/*
 * Copyright © 2015-2019 Intel Corporation
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

/** @file
 *
 * This file implements a pass that validates shader assembly.
 *
 * The restrictions implemented herein are intended to verify that instructions
 * in shader assembly do not violate restrictions documented in the graphics
 * programming reference manuals.
 *
 * The restrictions are difficult for humans to quickly verify due to their
 * complexity and abundance.
 *
 * It is critical that this code is thoroughly unit tested because false
 * results will lead developers astray, which is worse than having no validator
 * at all. Functional changes to this file without corresponding unit tests (in
 * test_eu_validate.cpp) will be rejected.
 */

#include <stdlib.h>
#include "brw_eu.h"
#include "brw_disasm_info.h"

typedef struct brw_hw_decoded_inst {
   const brw_inst *raw;

   enum opcode opcode;

   unsigned exec_size;
   unsigned access_mode;

   enum brw_conditional_mod cond_modifier;
   enum brw_predicate pred_control;
   bool saturate;

   bool has_dst;
   struct {
      enum brw_reg_file file;
      enum brw_reg_type type;
      unsigned address_mode;

      /* These are already physical register numbers. */
      unsigned nr;
      unsigned subnr;

      unsigned hstride;
   } dst;

   unsigned num_sources;
   struct {
      enum brw_reg_file file;
      enum brw_reg_type type;
      unsigned address_mode;
      bool negate;
      bool abs;

      /* These are already physical register numbers. */
      unsigned nr;
      unsigned subnr;

      unsigned vstride;
      unsigned width;
      unsigned hstride;
   } src[3];
} brw_hw_decoded_inst;

/* We're going to do lots of string concatenation, so this should help. */
struct string {
   char *str;
   size_t len;
};

static void
cat(struct string *dest, const struct string src)
{
   dest->str = realloc(dest->str, dest->len + src.len + 1);
   memcpy(dest->str + dest->len, src.str, src.len);
   dest->str[dest->len + src.len] = '\0';
   dest->len = dest->len + src.len;
}
#define CAT(dest, src) cat(&dest, (struct string){src, strlen(src)})

static bool
contains(const struct string haystack, const struct string needle)
{
   return haystack.str && memmem(haystack.str, haystack.len,
                                 needle.str, needle.len) != NULL;
}
#define CONTAINS(haystack, needle) \
   contains(haystack, (struct string){needle, strlen(needle)})

#define error(str)   "\tERROR: " str "\n"
#define ERROR_INDENT "\t       "

#define ERROR(msg) ERROR_IF(true, msg)
#define ERROR_IF(cond, msg)                             \
   do {                                                 \
      if ((cond) && !CONTAINS(error_msg, error(msg))) { \
         CAT(error_msg, error(msg));                    \
      }                                                 \
   } while(0)

#define RETURN_ERROR(msg) RETURN_ERROR_IF(true, msg)
#define RETURN_ERROR_IF(cond, msg)                      \
   do {                                                 \
      if ((cond) && !CONTAINS(error_msg, error(msg))) { \
         CAT(error_msg, error(msg));                    \
         return error_msg;                              \
      }                                                 \
   } while(0)

#define STRIDE(stride) (stride != 0 ? 1 << ((stride) - 1) : 0)
#define WIDTH(width)   (1 << (width))

static bool
inst_is_send(const brw_hw_decoded_inst *inst)
{
   switch (inst->opcode) {
   case BRW_OPCODE_SEND:
   case BRW_OPCODE_SENDC:
   case BRW_OPCODE_SENDS:
   case BRW_OPCODE_SENDSC:
      return true;
   default:
      return false;
   }
}

static bool
inst_is_split_send(const struct brw_isa_info *isa, const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   if (devinfo->ver >= 12) {
      return inst_is_send(inst);
   } else {
      switch (inst->opcode) {
      case BRW_OPCODE_SENDS:
      case BRW_OPCODE_SENDSC:
         return true;
      default:
         return false;
      }
   }
}

static unsigned
signed_type(unsigned type)
{
   return brw_type_is_uint(type) ? (type | BRW_TYPE_BASE_SINT) : type;
}

static bool
inst_is_raw_move(const brw_hw_decoded_inst *inst)
{
   unsigned dst_type = signed_type(inst->dst.type);
   unsigned src_type = signed_type(inst->src[0].type);

   if (inst->src[0].file == IMM) {
      /* FIXME: not strictly true */
      if (brw_type_is_vector_imm(inst->src[0].type))
         return false;
   } else if (inst->src[0].negate || inst->src[0].abs) {
      return false;
   }

   return inst->opcode == BRW_OPCODE_MOV &&
          !inst->saturate &&
          dst_type == src_type;
}

static bool
dst_is_null(const brw_hw_decoded_inst *inst)
{
   return inst->dst.file == ARF && inst->dst.nr == BRW_ARF_NULL;
}

static bool
src0_is_null(const brw_hw_decoded_inst *inst)
{
   return inst->src[0].address_mode == BRW_ADDRESS_DIRECT &&
          inst->src[0].file == ARF &&
          inst->src[0].nr == BRW_ARF_NULL;
}

static bool
src1_is_null(const brw_hw_decoded_inst *inst)
{
   assert(inst->src[1].address_mode == BRW_ADDRESS_DIRECT);
   return inst->src[1].file == ARF &&
          inst->src[1].nr == BRW_ARF_NULL;
}

static bool
src0_is_acc(const brw_hw_decoded_inst *inst)
{
   return inst->src[0].address_mode == BRW_ADDRESS_DIRECT &&
          inst->src[0].file == ARF &&
          (inst->src[0].nr & 0xF0) == BRW_ARF_ACCUMULATOR;
}

static bool
src1_is_acc(const brw_hw_decoded_inst *inst)
{
   assert(inst->src[1].address_mode == BRW_ADDRESS_DIRECT);
   return inst->src[1].file == ARF &&
          (inst->src[1].nr & 0xF0) == BRW_ARF_ACCUMULATOR;
}

static bool
src_has_scalar_region(const brw_hw_decoded_inst *inst, int src)
{
   return inst->src[src].vstride == 0 &&
          inst->src[src].width == 1 &&
          inst->src[src].hstride == 0;
}

static struct string
invalid_values(const struct brw_isa_info *isa, const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   if (devinfo->ver >= 12) {
      unsigned qtr_ctrl = brw_inst_qtr_control(devinfo, inst->raw);
      unsigned nib_ctrl =
         devinfo->ver == 12 ? brw_inst_nib_control(devinfo, inst->raw) : 0;

      unsigned chan_off = (qtr_ctrl * 2 + nib_ctrl) << 2;
      ERROR_IF(chan_off % inst->exec_size != 0,
               "The execution size must be a factor of the chosen offset");
   }

   return error_msg;
}

static struct string
sources_not_null(const struct brw_isa_info *isa,
                 const brw_hw_decoded_inst *inst)
{
   struct string error_msg = { .str = NULL, .len = 0 };

   /* Nothing to test. 3-src instructions can only have GRF sources, and
    * there's no bit to control the file.
    */
   if (inst->num_sources == 3)
      return (struct string){};

   /* Nothing to test.  Split sends can only encode a file in sources that are
    * allowed to be NULL.
    */
   if (inst_is_split_send(isa, inst))
      return (struct string){};

   if (inst->num_sources >= 1 && inst->opcode != BRW_OPCODE_SYNC)
      ERROR_IF(src0_is_null(inst), "src0 is null");

   if (inst->num_sources == 2)
      ERROR_IF(src1_is_null(inst), "src1 is null");

   return error_msg;
}

static bool
inst_uses_src_acc(const struct brw_isa_info *isa,
                  const brw_hw_decoded_inst *inst)
{
   /* Check instructions that use implicit accumulator sources */
   switch (inst->opcode) {
   case BRW_OPCODE_MAC:
   case BRW_OPCODE_MACH:
      return true;
   default:
      break;
   }

   /* FIXME: support 3-src instructions */
   assert(inst->num_sources < 3);

   return src0_is_acc(inst) || (inst->num_sources > 1 && src1_is_acc(inst));
}

static struct string
send_restrictions(const struct brw_isa_info *isa,
                  const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst_is_split_send(isa, inst)) {
      ERROR_IF(inst->src[1].file == ARF &&
               inst->src[1].nr != BRW_ARF_NULL,
               "src1 of split send must be a GRF or NULL");

      ERROR_IF(brw_inst_eot(devinfo, inst->raw) &&
               inst->src[0].nr < 112,
               "send with EOT must use g112-g127");
      ERROR_IF(brw_inst_eot(devinfo, inst->raw) &&
               inst->src[1].file == FIXED_GRF &&
               inst->src[1].nr < 112,
               "send with EOT must use g112-g127");

      if (inst->src[0].file == FIXED_GRF && inst->src[1].file == FIXED_GRF) {
         /* Assume minimums if we don't know */
         unsigned mlen = 1;
         if (!brw_inst_send_sel_reg32_desc(devinfo, inst->raw)) {
            const uint32_t desc = brw_inst_send_desc(devinfo, inst->raw);
            mlen = brw_message_desc_mlen(devinfo, desc) / reg_unit(devinfo);
         }

         unsigned ex_mlen = 1;
         if (!brw_inst_send_sel_reg32_ex_desc(devinfo, inst->raw)) {
            const uint32_t ex_desc = brw_inst_sends_ex_desc(devinfo, inst->raw);
            ex_mlen = brw_message_ex_desc_ex_mlen(devinfo, ex_desc) /
                      reg_unit(devinfo);
         }
         const unsigned src0_reg_nr = inst->src[0].nr;
         const unsigned src1_reg_nr = inst->src[1].nr;
         ERROR_IF((src0_reg_nr <= src1_reg_nr &&
                   src1_reg_nr < src0_reg_nr + mlen) ||
                  (src1_reg_nr <= src0_reg_nr &&
                   src0_reg_nr < src1_reg_nr + ex_mlen),
                   "split send payloads must not overlap");
      }
   } else if (inst_is_send(inst)) {
      ERROR_IF(inst->src[0].address_mode != BRW_ADDRESS_DIRECT,
               "send must use direct addressing");

      ERROR_IF(inst->src[0].file != FIXED_GRF,
               "send from non-GRF");
      ERROR_IF(brw_inst_eot(devinfo, inst->raw) &&
               inst->src[0].nr < 112,
               "send with EOT must use g112-g127");

      ERROR_IF(!dst_is_null(inst) &&
               (inst->dst.nr + brw_inst_rlen(devinfo, inst->raw) > 127) &&
               (inst->src[0].nr + brw_inst_mlen(devinfo, inst->raw) > inst->dst.nr),
               "r127 must not be used for return address when there is "
               "a src and dest overlap");
   }

   return error_msg;
}

static bool
is_unsupported_inst(const struct brw_isa_info *isa,
                    const brw_inst *inst)
{
   return brw_inst_opcode(isa, inst) == BRW_OPCODE_ILLEGAL;
}

/**
 * Returns whether a combination of two types would qualify as mixed float
 * operation mode
 */
static inline bool
types_are_mixed_float(enum brw_reg_type t0, enum brw_reg_type t1)
{
   return (t0 == BRW_TYPE_F && t1 == BRW_TYPE_HF) ||
          (t1 == BRW_TYPE_F && t0 == BRW_TYPE_HF);
}

static enum brw_reg_type
execution_type_for_type(enum brw_reg_type type)
{
   switch (type) {
   case BRW_TYPE_DF:
   case BRW_TYPE_F:
   case BRW_TYPE_HF:
      return type;

   case BRW_TYPE_VF:
      return BRW_TYPE_F;

   case BRW_TYPE_Q:
   case BRW_TYPE_UQ:
      return BRW_TYPE_Q;

   case BRW_TYPE_D:
   case BRW_TYPE_UD:
      return BRW_TYPE_D;

   case BRW_TYPE_W:
   case BRW_TYPE_UW:
   case BRW_TYPE_B:
   case BRW_TYPE_UB:
   case BRW_TYPE_V:
   case BRW_TYPE_UV:
      return BRW_TYPE_W;
   default:
      unreachable("invalid type");
   }
}

/**
 * Returns the execution type of an instruction \p inst
 */
static enum brw_reg_type
execution_type(const brw_hw_decoded_inst *inst)
{
   enum brw_reg_type src0_exec_type, src1_exec_type;

   /* Execution data type is independent of destination data type, except in
    * mixed F/HF instructions.
    */
   enum brw_reg_type dst_exec_type = inst->dst.type;

   src0_exec_type = execution_type_for_type(inst->src[0].type);
   if (inst->num_sources == 1) {
      if (src0_exec_type == BRW_TYPE_HF)
         return dst_exec_type;
      return src0_exec_type;
   }

   src1_exec_type = execution_type_for_type(inst->src[1].type);
   if (types_are_mixed_float(src0_exec_type, src1_exec_type) ||
       types_are_mixed_float(src0_exec_type, dst_exec_type) ||
       types_are_mixed_float(src1_exec_type, dst_exec_type)) {
      return BRW_TYPE_F;
   }

   if (src0_exec_type == src1_exec_type)
      return src0_exec_type;

   if (src0_exec_type == BRW_TYPE_Q ||
       src1_exec_type == BRW_TYPE_Q)
      return BRW_TYPE_Q;

   if (src0_exec_type == BRW_TYPE_D ||
       src1_exec_type == BRW_TYPE_D)
      return BRW_TYPE_D;

   if (src0_exec_type == BRW_TYPE_W ||
       src1_exec_type == BRW_TYPE_W)
      return BRW_TYPE_W;

   if (src0_exec_type == BRW_TYPE_DF ||
       src1_exec_type == BRW_TYPE_DF)
      return BRW_TYPE_DF;

   unreachable("not reached");
}

/**
 * Returns whether a region is packed
 *
 * A region is packed if its elements are adjacent in memory, with no
 * intervening space, no overlap, and no replicated values.
 */
static bool
is_packed(unsigned vstride, unsigned width, unsigned hstride)
{
   if (vstride == width) {
      if (vstride == 1) {
         return hstride == 0;
      } else {
         return hstride == 1;
      }
   }

   return false;
}

/**
 * Returns whether a region is linear
 *
 * A region is linear if its elements do not overlap and are not replicated.
 * Unlike a packed region, intervening space (i.e. strided values) is allowed.
 */
static bool
is_linear(unsigned vstride, unsigned width, unsigned hstride)
{
   return vstride == width * hstride ||
          (hstride == 0 && width == 1);
}

/**
 * Returns whether an instruction is an explicit or implicit conversion
 * to/from half-float.
 */
static bool
is_half_float_conversion(const brw_hw_decoded_inst *inst)
{
   enum brw_reg_type dst_type = inst->dst.type;

   enum brw_reg_type src0_type = inst->src[0].type;

   if (dst_type != src0_type &&
       (dst_type == BRW_TYPE_HF || src0_type == BRW_TYPE_HF)) {
      return true;
   } else if (inst->num_sources > 1) {
      enum brw_reg_type src1_type = inst->src[1].type;
      return dst_type != src1_type &&
            (dst_type == BRW_TYPE_HF ||
             src1_type == BRW_TYPE_HF);
   }

   return false;
}

/*
 * Returns whether an instruction is using mixed float operation mode
 */
static bool
is_mixed_float(const brw_hw_decoded_inst *inst)
{
   if (inst_is_send(inst))
      return false;

   if (!inst->has_dst)
      return false;

   /* FIXME: support 3-src instructions */
   assert(inst->num_sources < 3);

   enum brw_reg_type dst_type = inst->dst.type;
   enum brw_reg_type src0_type = inst->src[0].type;

   if (inst->num_sources == 1)
      return types_are_mixed_float(src0_type, dst_type);

   enum brw_reg_type src1_type = inst->src[1].type;

   return types_are_mixed_float(src0_type, src1_type) ||
          types_are_mixed_float(src0_type, dst_type) ||
          types_are_mixed_float(src1_type, dst_type);
}

/**
 * Returns whether an instruction is an explicit or implicit conversion
 * to/from byte.
 */
static bool
is_byte_conversion(const struct brw_isa_info *isa,
                   const brw_hw_decoded_inst *inst)
{
   enum brw_reg_type dst_type = inst->dst.type;

   enum brw_reg_type src0_type = inst->src[0].type;

   if (dst_type != src0_type &&
       (brw_type_size_bytes(dst_type) == 1 ||
        brw_type_size_bytes(src0_type) == 1)) {
      return true;
   } else if (inst->num_sources > 1) {
      enum brw_reg_type src1_type = inst->src[1].type;
      return dst_type != src1_type &&
            (brw_type_size_bytes(dst_type) == 1 ||
             brw_type_size_bytes(src1_type) == 1);
   }

   return false;
}

/**
 * Checks restrictions listed in "General Restrictions Based on Operand Types"
 * in the "Register Region Restrictions" section.
 */
static struct string
general_restrictions_based_on_operand_types(const struct brw_isa_info *isa,
                                            const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst_is_send(inst))
      return error_msg;

   if (devinfo->ver >= 11) {
      /* A register type of B or UB for DPAS actually means 4 bytes packed into
       * a D or UD, so it is allowed.
       */
      if (inst->num_sources == 3 && inst->opcode != BRW_OPCODE_DPAS) {
         ERROR_IF(brw_type_size_bytes(inst->src[1].type) == 1 ||
                  brw_type_size_bytes(inst->src[2].type) == 1,
                  "Byte data type is not supported for src1/2 register regioning. This includes "
                  "byte broadcast as well.");
      }
      if (inst->num_sources == 2) {
         ERROR_IF(brw_type_size_bytes(inst->src[1].type) == 1,
                  "Byte data type is not supported for src1 register regioning. This includes "
                  "byte broadcast as well.");
      }
   }

   enum brw_reg_type dst_type = inst->dst.type;

   ERROR_IF(dst_type == BRW_TYPE_DF &&
            !devinfo->has_64bit_float,
            "64-bit float destination, but platform does not support it");

   ERROR_IF((dst_type == BRW_TYPE_Q ||
             dst_type == BRW_TYPE_UQ) &&
            !devinfo->has_64bit_int,
            "64-bit int destination, but platform does not support it");

   for (unsigned s = 0; s < inst->num_sources; s++) {
      enum brw_reg_type src_type = inst->src[s].type;

      ERROR_IF(src_type == BRW_TYPE_DF &&
               !devinfo->has_64bit_float,
               "64-bit float source, but platform does not support it");

      ERROR_IF((src_type == BRW_TYPE_Q ||
                src_type == BRW_TYPE_UQ) &&
               !devinfo->has_64bit_int,
               "64-bit int source, but platform does not support it");
      if (inst->access_mode == BRW_ALIGN_16 &&
          inst->num_sources == 3 && brw_type_size_bytes(src_type) > 4) {
         /* From the Broadwell PRM, Volume 7 "3D Media GPGPU", page 944:
          *
          *    "This is applicable to 32b datatypes and 16b datatype. 64b
          *    datatypes cannot use the replicate control."
          */
         switch (s) {
         case 0:
            ERROR_IF(brw_inst_3src_a16_src0_rep_ctrl(devinfo, inst->raw),
                     "RepCtrl must be zero for 64-bit source 0");
            break;
         case 1:
            ERROR_IF(brw_inst_3src_a16_src1_rep_ctrl(devinfo, inst->raw),
                     "RepCtrl must be zero for 64-bit source 1");
            break;
         case 2:
            ERROR_IF(brw_inst_3src_a16_src2_rep_ctrl(devinfo, inst->raw),
                     "RepCtrl must be zero for 64-bit source 2");
            break;
         default: unreachable("invalid src");
         }
      }
   }

   if (inst->num_sources == 3)
      return error_msg;

   if (inst->exec_size == 1)
      return error_msg;

   if (!inst->has_dst)
      return error_msg;

   if (inst->opcode == BRW_OPCODE_MATH &&
       intel_needs_workaround(devinfo, 22016140776)) {
      /* Wa_22016140776:
       *
       *    Scalar broadcast on HF math (packed or unpacked) must not be
       *    used.  Compiler must use a mov instruction to expand the scalar
       *    value to a vector before using in a HF (packed or unpacked)
       *    math operation.
       */
      ERROR_IF(inst->src[0].type == BRW_TYPE_HF &&
               src_has_scalar_region(inst, 0),
               "Scalar broadcast on HF math (packed or unpacked) must not "
               "be used.");

      if (inst->num_sources > 1) {
         ERROR_IF(inst->src[1].type == BRW_TYPE_HF &&
                  src_has_scalar_region(inst, 1),
                  "Scalar broadcast on HF math (packed or unpacked) must not "
                  "be used.");
      }
   }

   /* The PRMs say:
    *
    *    Where n is the largest element size in bytes for any source or
    *    destination operand type, ExecSize * n must be <= 64.
    *
    * But we do not attempt to enforce it, because it is implied by other
    * rules:
    *
    *    - that the destination stride must match the execution data type
    *    - sources may not span more than two adjacent GRF registers
    *    - destination may not span more than two adjacent GRF registers
    *
    * In fact, checking it would weaken testing of the other rules.
    */

   unsigned dst_stride = inst->dst.hstride;
   bool dst_type_is_byte =
      inst->dst.type == BRW_TYPE_B ||
      inst->dst.type == BRW_TYPE_UB;

   if (dst_type_is_byte) {
      if (is_packed(inst->exec_size * dst_stride, inst->exec_size, dst_stride)) {
         if (!inst_is_raw_move(inst))
            ERROR("Only raw MOV supports a packed-byte destination");
         return error_msg;
      }
   }

   unsigned exec_type = execution_type(inst);
   unsigned exec_type_size = brw_type_size_bytes(exec_type);
   unsigned dst_type_size = brw_type_size_bytes(dst_type);

   if (is_byte_conversion(isa, inst)) {
      /* From the BDW+ PRM, Volume 2a, Command Reference, Instructions - MOV:
       *
       *    "There is no direct conversion from B/UB to DF or DF to B/UB.
       *     There is no direct conversion from B/UB to Q/UQ or Q/UQ to B/UB."
       *
       * Even if these restrictions are listed for the MOV instruction, we
       * validate this more generally, since there is the possibility
       * of implicit conversions from other instructions.
       */
      enum brw_reg_type src0_type = inst->src[0].type;
      enum brw_reg_type src1_type = inst->num_sources > 1 ?
                                    inst->src[1].type : 0;

      ERROR_IF(brw_type_size_bytes(dst_type) == 1 &&
               (brw_type_size_bytes(src0_type) == 8 ||
                (inst->num_sources > 1 && brw_type_size_bytes(src1_type) == 8)),
               "There are no direct conversions between 64-bit types and B/UB");

      ERROR_IF(brw_type_size_bytes(dst_type) == 8 &&
               (brw_type_size_bytes(src0_type) == 1 ||
                (inst->num_sources > 1 && brw_type_size_bytes(src1_type) == 1)),
               "There are no direct conversions between 64-bit types and B/UB");
   }

   if (is_half_float_conversion(inst)) {
      /**
       * A helper to validate used in the validation of the following restriction
       * from the BDW+ PRM, Volume 2a, Command Reference, Instructions - MOV:
       *
       *    "There is no direct conversion from HF to DF or DF to HF.
       *     There is no direct conversion from HF to Q/UQ or Q/UQ to HF."
       *
       * Even if these restrictions are listed for the MOV instruction, we
       * validate this more generally, since there is the possibility
       * of implicit conversions from other instructions, such us implicit
       * conversion from integer to HF with the ADD instruction in SKL+.
       */
      enum brw_reg_type src0_type = inst->src[0].type;
      enum brw_reg_type src1_type = inst->num_sources > 1 ?
                                    inst->src[1].type : 0;
      ERROR_IF(dst_type == BRW_TYPE_HF &&
               (brw_type_size_bytes(src0_type) == 8 ||
                (inst->num_sources > 1 && brw_type_size_bytes(src1_type) == 8)),
               "There are no direct conversions between 64-bit types and HF");

      ERROR_IF(brw_type_size_bytes(dst_type) == 8 &&
               (src0_type == BRW_TYPE_HF ||
                (inst->num_sources > 1 && src1_type == BRW_TYPE_HF)),
               "There are no direct conversions between 64-bit types and HF");

      /* From the BDW+ PRM:
       *
       *   "Conversion between Integer and HF (Half Float) must be
       *    DWord-aligned and strided by a DWord on the destination."
       *
       * Also, the above restrictions seems to be expanded on CHV and SKL+ by:
       *
       *   "There is a relaxed alignment rule for word destinations. When
       *    the destination type is word (UW, W, HF), destination data types
       *    can be aligned to either the lowest word or the second lowest
       *    word of the execution channel. This means the destination data
       *    words can be either all in the even word locations or all in the
       *    odd word locations."
       *
       * We do not implement the second rule as is though, since empirical
       * testing shows inconsistencies:
       *   - It suggests that packed 16-bit is not allowed, which is not true.
       *   - It suggests that conversions from Q/DF to W (which need to be
       *     64-bit aligned on the destination) are not possible, which is
       *     not true.
       *
       * So from this rule we only validate the implication that conversions
       * from F to HF need to be DWord strided (except in Align1 mixed
       * float mode where packed fp16 destination is allowed so long as the
       * destination is oword-aligned).
       *
       * Finally, we only validate this for Align1 because Align16 always
       * requires packed destinations, so these restrictions can't possibly
       * apply to Align16 mode.
       */
      if (inst->access_mode == BRW_ALIGN_1) {
         if ((dst_type == BRW_TYPE_HF &&
              (brw_type_is_int(src0_type) ||
               (inst->num_sources > 1 && brw_type_is_int(src1_type)))) ||
             (brw_type_is_int(dst_type) &&
              (src0_type == BRW_TYPE_HF ||
               (inst->num_sources > 1 && src1_type == BRW_TYPE_HF)))) {
            ERROR_IF(dst_stride * dst_type_size != 4,
                     "Conversions between integer and half-float must be "
                     "strided by a DWord on the destination");

            ERROR_IF(inst->dst.subnr % 4 != 0,
                     "Conversions between integer and half-float must be "
                     "aligned to a DWord on the destination");
         } else if (dst_type == BRW_TYPE_HF) {
            ERROR_IF(dst_stride != 2 &&
                     !(is_mixed_float(inst) &&
                       dst_stride == 1 && inst->dst.subnr % 16 == 0),
                     "Conversions to HF must have either all words in even "
                     "word locations or all words in odd word locations or "
                     "be mixed-float with Oword-aligned packed destination");
         }
      }
   }

   /* There are special regioning rules for mixed-float mode in CHV and SKL that
    * override the general rule for the ratio of sizes of the destination type
    * and the execution type. We will add validation for those in a later patch.
    */
   bool validate_dst_size_and_exec_size_ratio = !is_mixed_float(inst);

   if (validate_dst_size_and_exec_size_ratio &&
       exec_type_size > dst_type_size) {
      if (!(dst_type_is_byte && inst_is_raw_move(inst))) {
         ERROR_IF(dst_stride * dst_type_size != exec_type_size,
                  "Destination stride must be equal to the ratio of the sizes "
                  "of the execution data type to the destination type");
      }

      unsigned subreg = inst->dst.subnr;

      if (inst->access_mode == BRW_ALIGN_1 &&
          inst->dst.address_mode == BRW_ADDRESS_DIRECT) {
         /* The i965 PRM says:
          *
          *    Implementation Restriction: The relaxed alignment rule for byte
          *    destination (#10.5) is not supported.
          */
         if (dst_type_is_byte) {
            ERROR_IF(subreg % exec_type_size != 0 &&
                     subreg % exec_type_size != 1,
                     "Destination subreg must be aligned to the size of the "
                     "execution data type (or to the next lowest byte for byte "
                     "destinations)");
         } else {
            ERROR_IF(subreg % exec_type_size != 0,
                     "Destination subreg must be aligned to the size of the "
                     "execution data type");
         }
      }
   }

   return error_msg;
}

/**
 * Checks restrictions listed in "General Restrictions on Regioning Parameters"
 * in the "Register Region Restrictions" section.
 */
static struct string
general_restrictions_on_region_parameters(const struct brw_isa_info *isa,
                                          const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst->num_sources == 3)
      return (struct string){};

   /* Split sends don't have the bits in the instruction to encode regions so
    * there's nothing to check.
    */
   if (inst_is_split_send(isa, inst))
      return (struct string){};

   if (inst->access_mode == BRW_ALIGN_16) {
      if (inst->has_dst && !dst_is_null(inst))
         ERROR_IF(inst->dst.hstride != 1,
                  "Destination Horizontal Stride must be 1");

      if (inst->num_sources >= 1) {
         ERROR_IF(inst->src[0].file != IMM &&
                  inst->src[0].vstride != 0 &&
                  inst->src[0].vstride != 2 &&
                  inst->src[0].vstride != 4,
                  "In Align16 mode, only VertStride of 0, 2, or 4 is allowed");
      }

      if (inst->num_sources == 2) {
         ERROR_IF(inst->src[1].file != IMM &&
                  inst->src[1].vstride != 0 &&
                  inst->src[1].vstride != 2 &&
                  inst->src[1].vstride != 4,
                  "In Align16 mode, only VertStride of 0, 2, or 4 is allowed");
      }

      return error_msg;
   }

   for (unsigned i = 0; i < inst->num_sources; i++) {
      if (inst->src[i].file == IMM)
         continue;

      enum brw_reg_type type = inst->src[i].type;
      unsigned element_size = brw_type_size_bytes(type);
      unsigned subreg = inst->src[i].subnr;
      unsigned vstride = inst->src[i].vstride;
      unsigned width = inst->src[i].width;
      unsigned hstride = inst->src[i].hstride;

      /* ExecSize must be greater than or equal to Width. */
      ERROR_IF(inst->exec_size < width, "ExecSize must be greater than or equal "
                                        "to Width");

      /* If ExecSize = Width and HorzStride ≠ 0,
       * VertStride must be set to Width * HorzStride.
       */
      if (inst->exec_size == width && hstride != 0) {
         ERROR_IF(vstride != width * hstride,
                  "If ExecSize = Width and HorzStride ≠ 0, "
                  "VertStride must be set to Width * HorzStride");
      }

      /* If Width = 1, HorzStride must be 0 regardless of the values of
       * ExecSize and VertStride.
       */
      if (width == 1) {
         ERROR_IF(hstride != 0,
                  "If Width = 1, HorzStride must be 0 regardless "
                  "of the values of ExecSize and VertStride");
      }

      /* If ExecSize = Width = 1, both VertStride and HorzStride must be 0. */
      if (inst->exec_size == 1 && width == 1) {
         ERROR_IF(vstride != 0 || hstride != 0,
                  "If ExecSize = Width = 1, both VertStride "
                  "and HorzStride must be 0");
      }

      /* If VertStride = HorzStride = 0, Width must be 1 regardless of the
       * value of ExecSize.
       */
      if (vstride == 0 && hstride == 0) {
         ERROR_IF(width != 1,
                  "If VertStride = HorzStride = 0, Width must be "
                  "1 regardless of the value of ExecSize");
      }

      /* VertStride must be used to cross GRF register boundaries. This rule
       * implies that elements within a 'Width' cannot cross GRF boundaries.
       */
      if (inst->src[i].file == FIXED_GRF) {
         unsigned rowbase = subreg;
         assert(util_is_power_of_two_nonzero(reg_unit(devinfo)));
         unsigned grf_size_shift = ffs(REG_SIZE * reg_unit(devinfo)) - 1;

         for (int y = 0; y < inst->exec_size / width; y++) {
            bool spans_grfs = false;
            unsigned offset = rowbase;
            unsigned first_grf = offset >> grf_size_shift;

            for (int x = 0; x < width; x++) {
               const unsigned end_byte = offset + (element_size - 1);
               const unsigned end_grf = end_byte >> grf_size_shift;
               spans_grfs = end_grf != first_grf;
               if (spans_grfs)
                  break;
               offset += hstride * element_size;
            }

            rowbase += vstride * element_size;

            if (spans_grfs) {
               ERROR("VertStride must be used to cross GRF register boundaries");
               break;
            }
         }
      }
   }

   /* Dst.HorzStride must not be 0. */
   if (inst->has_dst && !dst_is_null(inst)) {
      ERROR_IF(inst->dst.hstride == 0,
               "Destination Horizontal Stride must not be 0");
   }

   return error_msg;
}

static struct string
special_restrictions_for_mixed_float_mode(const struct brw_isa_info *isa,
                                          const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   const unsigned opcode = inst->opcode;
   if (inst->num_sources >= 3)
      return error_msg;

   if (!is_mixed_float(inst))
      return error_msg;

   bool is_align16 = inst->access_mode == BRW_ALIGN_16;

   enum brw_reg_type src0_type = inst->src[0].type;
   enum brw_reg_type src1_type = inst->num_sources > 1 ?
                                 inst->src[1].type : 0;
   enum brw_reg_type dst_type = inst->dst.type;

   unsigned dst_stride = inst->dst.hstride;
   bool dst_is_packed = is_packed(inst->exec_size * dst_stride, inst->exec_size, dst_stride);

   /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
    * Float Operations:
    *
    *    "Indirect addressing on source is not supported when source and
    *     destination data types are mixed float."
    */
   ERROR_IF(inst->src[0].address_mode != BRW_ADDRESS_DIRECT ||
            (inst->num_sources > 1 &&
             inst->src[1].address_mode != BRW_ADDRESS_DIRECT),
            "Indirect addressing on source is not supported when source and "
            "destination data types are mixed float");

   /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
    * Float Operations:
    *
    *    "No SIMD16 in mixed mode when destination is f32. Instruction
    *     execution size must be no more than 8."
    */
   ERROR_IF(inst->exec_size > 8 && devinfo->ver < 20 &&
            dst_type == BRW_TYPE_F &&
            opcode != BRW_OPCODE_MOV,
            "Mixed float mode with 32-bit float destination is limited "
            "to SIMD8");

   if (is_align16) {
      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *   "In Align16 mode, when half float and float data types are mixed
       *    between source operands OR between source and destination operands,
       *    the register content are assumed to be packed."
       *
       * Since Align16 doesn't have a concept of horizontal stride (or width),
       * it means that vertical stride must always be 4, since 0 and 2 would
       * lead to replicated data, and any other value is disallowed in Align16.
       */
      ERROR_IF(inst->src[0].vstride != 4,
               "Align16 mixed float mode assumes packed data (vstride must be 4");

      ERROR_IF(inst->num_sources >= 2 &&
               inst->src[1].vstride != 4,
               "Align16 mixed float mode assumes packed data (vstride must be 4");

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *   "For Align16 mixed mode, both input and output packed f16 data
       *    must be oword aligned, no oword crossing in packed f16."
       *
       * The previous rule requires that Align16 operands are always packed,
       * and since there is only one bit for Align16 subnr, which represents
       * offsets 0B and 16B, this rule is always enforced and we don't need to
       * validate it.
       */

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "No SIMD16 in mixed mode when destination is packed f16 for both
       *     Align1 and Align16."
       *
       * And:
       *
       *   "In Align16 mode, when half float and float data types are mixed
       *    between source operands OR between source and destination operands,
       *    the register content are assumed to be packed."
       *
       * Which implies that SIMD16 is not available in Align16. This is further
       * confirmed by:
       *
       *    "For Align16 mixed mode, both input and output packed f16 data
       *     must be oword aligned, no oword crossing in packed f16"
       *
       * Since oword-aligned packed f16 data would cross oword boundaries when
       * the execution size is larger than 8.
       */
      ERROR_IF(inst->exec_size > 8, "Align16 mixed float mode is limited to SIMD8");

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "No accumulator read access for Align16 mixed float."
       */
      ERROR_IF(inst_uses_src_acc(isa, inst),
               "No accumulator read access for Align16 mixed float");
   } else {
      assert(!is_align16);

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "No SIMD16 in mixed mode when destination is packed f16 for both
       *     Align1 and Align16."
       */
      ERROR_IF(inst->exec_size > 8 && dst_is_packed &&
               dst_type == BRW_TYPE_HF &&
               opcode != BRW_OPCODE_MOV,
               "Align1 mixed float mode is limited to SIMD8 when destination "
               "is packed half-float");

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "Math operations for mixed mode:
       *     - In Align1, f16 inputs need to be strided"
       */
      if (opcode == BRW_OPCODE_MATH) {
         if (src0_type == BRW_TYPE_HF) {
            ERROR_IF(inst->src[0].hstride <= 1,
                     "Align1 mixed mode math needs strided half-float inputs");
         }

         if (inst->num_sources >= 2 && src1_type == BRW_TYPE_HF) {
            ERROR_IF(inst->src[1].hstride <= 1,
                     "Align1 mixed mode math needs strided half-float inputs");
         }
      }

      if (dst_type == BRW_TYPE_HF && dst_stride == 1) {
         /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
          * Float Operations:
          *
          *    "In Align1, destination stride can be smaller than execution
          *     type. When destination is stride of 1, 16 bit packed data is
          *     updated on the destination. However, output packed f16 data
          *     must be oword aligned, no oword crossing in packed f16."
          *
          * The requirement of not crossing oword boundaries for 16-bit oword
          * aligned data means that execution size is limited to 8.
          */
         ERROR_IF(inst->dst.subnr % 16 != 0,
                  "Align1 mixed mode packed half-float output must be "
                  "oword aligned");
         ERROR_IF(inst->exec_size > 8,
                  "Align1 mixed mode packed half-float output must not "
                  "cross oword boundaries (max exec size is 8)");

         /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
          * Float Operations:
          *
          *    "When source is float or half float from accumulator register and
          *     destination is half float with a stride of 1, the source must
          *     register aligned. i.e., source must have offset zero."
          *
          * Align16 mixed float mode doesn't allow accumulator access on sources,
          * so we only need to check this for Align1.
          */
         if (src0_is_acc(inst) &&
             (src0_type == BRW_TYPE_F ||
              src0_type == BRW_TYPE_HF)) {
            ERROR_IF(inst->src[0].subnr != 0,
                     "Mixed float mode requires register-aligned accumulator "
                     "source reads when destination is packed half-float");

         }

         if (inst->num_sources > 1 &&
             src1_is_acc(inst) &&
             (src1_type == BRW_TYPE_F ||
              src1_type == BRW_TYPE_HF)) {
            ERROR_IF(inst->src[1].subnr != 0,
                     "Mixed float mode requires register-aligned accumulator "
                     "source reads when destination is packed half-float");
         }
      }

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "No swizzle is allowed when an accumulator is used as an implicit
       *     source or an explicit source in an instruction. i.e. when
       *     destination is half float with an implicit accumulator source,
       *     destination stride needs to be 2."
       *
       * FIXME: it is not quite clear what the first sentence actually means
       *        or its link to the implication described after it, so we only
       *        validate the explicit implication, which is clearly described.
       */
      if (dst_type == BRW_TYPE_HF &&
          inst_uses_src_acc(isa, inst)) {
         ERROR_IF(dst_stride != 2,
                  "Mixed float mode with implicit/explicit accumulator "
                  "source and half-float destination requires a stride "
                  "of 2 on the destination");
      }
   }

   return error_msg;
}

/**
 * Creates a \p grf_access_mask for an \p exec_size, \p element_size, and a
 * region
 *
 * A \p grf_access_mask is a 32-element array of uint8_t, where each uint8_t
 * is a bitmask of grfs accessed by the region.
 *
 * For instance the access mask of the source gX.1<4,2,2>F in an exec_size = 4
 * instruction would be
 *
 *    access_mask[0] = 0x01 (bytes 7-4 of the 1st grf)
 *    access_mask[1] = 0x01 (bytes 15-12 of the 1st grf)
 *    access_mask[2] = 0x01 (bytes 23-20 of the 1st grf)
 *    access_mask[3] = 0x01 (bytes 31-28 of the 1st grf)
 *    access_mask[4-31] = 0
 *
 * Before Xe2, gX<1,1,0>F in an exec_size == 16 would yield:
 *
 *    access_mask[0] = 0x01 (bytes 3-0 of the 1st grf)
 *    access_mask[1] = 0x01 (bytes 7-4 of the 1st grf)
 *      ...
 *    access_mask[7] = 0x01 (bytes 31-28 of the 1st grf)
 *    access_mask[8] = 0x02 (bytes 3-0 of the 2nd grf)
 *      ...
 *    access_mask[15] = 0x02 (bytes 31-28 of the 2nd grf)
 *    access_mask[16-31] = 0
 *
 * Whereas on Xe2, gX<1,1,0>F in an exec_size of 16 would yield:
 *
 *    access_mask[0] = 0x01 (bytes 3-0 of the 1st grf)
 *    access_mask[1] = 0x01 (bytes 7-4 of the 1st grf)
 *      ...
 *    access_mask[7] = 0x01 (bytes 31-28 of the 1st grf)
 *    access_mask[8] = 0x01 (bytes 35-32 of the 1st grf)
 *      ...
 *    access_mask[15] = 0x01 (bytes 63-60 of the 1st grf)
 *    access_mask[4-31] = 0
 *
 */
static void
grfs_accessed(const struct intel_device_info *devinfo,
              uint8_t grf_access_mask[static 32],
              unsigned exec_size, unsigned element_size, unsigned subreg,
              unsigned vstride, unsigned width, unsigned hstride)
{
   unsigned rowbase = subreg;
   unsigned element = 0;
   assert(util_is_power_of_two_nonzero(reg_unit(devinfo)));
   unsigned grf_size_shift = (5 - 1) + ffs(reg_unit(devinfo));

   for (int y = 0; y < exec_size / width; y++) {
      unsigned offset = rowbase;

      for (int x = 0; x < width; x++) {
         const unsigned start_grf = (offset >> grf_size_shift) % 8;
         const unsigned end_byte = offset + (element_size - 1);
         const unsigned end_grf = (end_byte >> grf_size_shift) % 8;
         grf_access_mask[element++] = (1 << start_grf) | (1 << end_grf);
         offset += hstride * element_size;
      }

      rowbase += vstride * element_size;
   }

   assert(element == 0 || element == exec_size);
}

/**
 * Returns the number of registers accessed according to the \p access_mask
 */
static int
registers_read(const uint8_t grfs_accessed[static 32])
{
   uint8_t all_read = 0;

   for (unsigned i = 0; i < 32; i++)
      all_read |= grfs_accessed[i];

   return util_bitcount(all_read);
}

/**
 * Checks restrictions listed in "Region Alignment Rules" in the "Register
 * Region Restrictions" section.
 */
static struct string
region_alignment_rules(const struct brw_isa_info *isa,
                       const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   uint8_t dst_access_mask[32] = {}, src_access_mask[2][32] = {};
   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst->num_sources == 3)
      return (struct string){};

   if (inst->access_mode == BRW_ALIGN_16)
      return (struct string){};

   if (inst_is_send(inst))
      return (struct string){};

   for (unsigned i = 0; i < inst->num_sources; i++) {
      /* In Direct Addressing mode, a source cannot span more than 2 adjacent
       * GRF registers.
       */
      if (inst->src[i].file != FIXED_GRF ||
          inst->src[i].address_mode != BRW_ADDRESS_DIRECT)
         continue;

      enum brw_reg_type type = inst->src[i].type;
      unsigned element_size = brw_type_size_bytes(type);
      unsigned subreg = inst->src[i].subnr;
      unsigned vstride = inst->src[i].vstride;
      unsigned width = inst->src[i].width;
      unsigned hstride = inst->src[i].hstride;

      grfs_accessed(devinfo, src_access_mask[i],
                    inst->exec_size, element_size, subreg,
                    vstride, width, hstride);

      unsigned num_vstride = inst->exec_size / width;
      unsigned num_hstride = width;
      unsigned vstride_elements = (num_vstride - 1) * vstride;
      unsigned hstride_elements = (num_hstride - 1) * hstride;
      unsigned offset = (vstride_elements + hstride_elements) * element_size +
                        subreg;
      ERROR_IF(offset >= 64 * reg_unit(devinfo),
               "A source cannot span more than 2 adjacent GRF registers");
   }

   if (!inst->has_dst || dst_is_null(inst))
      return error_msg;

   unsigned stride = inst->dst.hstride;
   enum brw_reg_type dst_type = inst->dst.type;
   unsigned element_size = brw_type_size_bytes(dst_type);
   unsigned subreg = inst->dst.subnr;
   unsigned offset = ((inst->exec_size - 1) * stride * element_size) + subreg;
   ERROR_IF(offset >= 64 * reg_unit(devinfo),
            "A destination cannot span more than 2 adjacent GRF registers");

   if (error_msg.str)
      return error_msg;

   grfs_accessed(devinfo, dst_access_mask, inst->exec_size, element_size, subreg,
                 inst->exec_size == 1 ? 0 : inst->exec_size * stride,
                 inst->exec_size == 1 ? 1 : inst->exec_size,
                 inst->exec_size == 1 ? 0 : stride);

   unsigned dst_regs = registers_read(dst_access_mask);

   /* The SKL PRM says:
    *
    *    When destination of MATH instruction spans two registers, the
    *    destination elements must be evenly split between the two registers.
    *
    * It is not known whether this restriction applies to KBL other Gens after
    * SKL.
    */
   if (inst->opcode == BRW_OPCODE_MATH) {
      if (dst_regs == 2) {
         unsigned upper_reg_writes = 0, lower_reg_writes = 0;

         for (unsigned i = 0; i < inst->exec_size; i++) {
            if (dst_access_mask[i] == 2) {
               upper_reg_writes++;
            } else {
               assert(dst_access_mask[i] == 1);
               lower_reg_writes++;
            }
         }

         ERROR_IF(upper_reg_writes != lower_reg_writes,
                  "Writes must be evenly split between the two "
                  "destination registers");
      }
   }

   return error_msg;
}

static struct string
vector_immediate_restrictions(const struct brw_isa_info *isa,
                              const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst->num_sources == 3 || inst->num_sources == 0 ||
       (devinfo->ver >= 12 && inst_is_send(inst)))
      return (struct string){};

   unsigned file = inst->src[inst->num_sources == 1 ? 0 : 1].file;
   if (file != IMM)
      return (struct string){};

   enum brw_reg_type dst_type = inst->dst.type;
   unsigned dst_type_size = brw_type_size_bytes(dst_type);
   unsigned dst_subreg = inst->dst.subnr;
   unsigned dst_stride = inst->dst.hstride;
   enum brw_reg_type type = inst->src[inst->num_sources == 1 ? 0 : 1].type;

   /* The PRMs say:
    *
    *    When an immediate vector is used in an instruction, the destination
    *    must be 128-bit aligned with destination horizontal stride equivalent
    *    to a word for an immediate integer vector (v) and equivalent to a
    *    DWord for an immediate float vector (vf).
    *
    * The text has not been updated for the addition of the immediate unsigned
    * integer vector type (uv) on SNB, but presumably the same restriction
    * applies.
    */
   switch (type) {
   case BRW_TYPE_V:
   case BRW_TYPE_UV:
   case BRW_TYPE_VF:
      ERROR_IF(dst_subreg % (128 / 8) != 0,
               "Destination must be 128-bit aligned in order to use immediate "
               "vector types");

      if (type == BRW_TYPE_VF) {
         ERROR_IF(dst_type_size * dst_stride != 4,
                  "Destination must have stride equivalent to dword in order "
                  "to use the VF type");
      } else {
         ERROR_IF(dst_type_size * dst_stride != 2,
                  "Destination must have stride equivalent to word in order "
                  "to use the V or UV type");
      }
      break;
   default:
      break;
   }

   return error_msg;
}

static struct string
special_requirements_for_handling_double_precision_data_types(
                                       const struct brw_isa_info *isa,
                                       const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst->num_sources == 3 || inst->num_sources == 0)
      return (struct string){};

   /* Split sends don't have types so there's no doubles there. */
   if (inst_is_split_send(isa, inst))
      return (struct string){};

   enum brw_reg_type exec_type = execution_type(inst);
   unsigned exec_type_size = brw_type_size_bytes(exec_type);

   enum brw_reg_type dst_type = inst->dst.type;
   unsigned dst_type_size = brw_type_size_bytes(dst_type);
   unsigned dst_hstride = inst->dst.hstride;
   unsigned dst_reg = inst->dst.nr;
   unsigned dst_subreg = inst->dst.subnr;
   unsigned dst_address_mode = inst->dst.address_mode;

   bool is_integer_dword_multiply =
      inst->opcode == BRW_OPCODE_MUL &&
      (inst->src[0].type == BRW_TYPE_D || inst->src[0].type == BRW_TYPE_UD) &&
      (inst->src[1].type == BRW_TYPE_D || inst->src[1].type == BRW_TYPE_UD);

   const bool is_double_precision =
      dst_type_size == 8 || exec_type_size == 8 || is_integer_dword_multiply;

   for (unsigned i = 0; i < inst->num_sources; i++) {
      enum brw_reg_file file = inst->src[i].file;
      if (file == IMM)
         continue;

      enum brw_reg_type type = inst->src[i].type;
      unsigned type_size = brw_type_size_bytes(type);
      unsigned address_mode = inst->src[i].address_mode;
      unsigned reg = inst->src[i].nr;
      unsigned subreg = inst->src[i].subnr;
      bool is_scalar_region = src_has_scalar_region(inst, i);
      unsigned vstride = inst->src[i].vstride;
      unsigned width = inst->src[i].width;
      unsigned hstride = inst->src[i].hstride;

      const unsigned src_stride = (hstride ? hstride : vstride) * type_size;
      const unsigned dst_stride = dst_hstride * dst_type_size;

      /* The PRMs say that for CHV, BXT:
       *
       *    When source or destination datatype is 64b or operation is integer
       *    DWord multiply, regioning in Align1 must follow these rules:
       *
       *    1. Source and Destination horizontal stride must be aligned to the
       *       same qword.
       *    2. Regioning must ensure Src.Vstride = Src.Width * Src.Hstride.
       *    3. Source and Destination offset must be the same, except the case
       *       of scalar source.
       *
       * We assume that the restriction applies to GLK as well.
       */
      if (is_double_precision &&
          inst->access_mode == BRW_ALIGN_1 &&
          intel_device_info_is_9lp(devinfo)) {
         ERROR_IF(!is_scalar_region &&
                  (src_stride % 8 != 0 ||
                   dst_stride % 8 != 0 ||
                   src_stride != dst_stride),
                  "Source and destination horizontal stride must equal and a "
                  "multiple of a qword when the execution type is 64-bit");

         ERROR_IF(vstride != width * hstride,
                  "Vstride must be Width * Hstride when the execution type is "
                  "64-bit");

         ERROR_IF(!is_scalar_region && dst_subreg != subreg,
                  "Source and destination offset must be the same when the "
                  "execution type is 64-bit");
      }

      /* The PRMs say that for CHV, BXT:
       *
       *    When source or destination datatype is 64b or operation is integer
       *    DWord multiply, indirect addressing must not be used.
       *
       * We assume that the restriction applies to GLK as well.
       */
      if (is_double_precision &&
          intel_device_info_is_9lp(devinfo)) {
         ERROR_IF(BRW_ADDRESS_REGISTER_INDIRECT_REGISTER == address_mode ||
                  BRW_ADDRESS_REGISTER_INDIRECT_REGISTER == dst_address_mode,
                  "Indirect addressing is not allowed when the execution type "
                  "is 64-bit");
      }

      /* The PRMs say that for CHV, BXT:
       *
       *    ARF registers must never be used with 64b datatype or when
       *    operation is integer DWord multiply.
       *
       * We assume that the restriction applies to GLK as well.
       *
       * We assume that the restriction does not apply to the null register.
       */
      if (is_double_precision &&
          intel_device_info_is_9lp(devinfo)) {
         ERROR_IF(inst->opcode == BRW_OPCODE_MAC ||
                  brw_inst_acc_wr_control(devinfo, inst->raw) ||
                  (ARF == file &&
                   reg != BRW_ARF_NULL) ||
                  (ARF == inst->dst.file &&
                   dst_reg != BRW_ARF_NULL),
                  "Architecture registers cannot be used when the execution "
                  "type is 64-bit");
      }

      /* From the hardware spec section "Register Region Restrictions":
       *
       * There are two rules:
       *
       * "In case of all floating point data types used in destination:" and
       *
       * "In case where source or destination datatype is 64b or operation is
       *  integer DWord multiply:"
       *
       * both of which list the same restrictions:
       *
       *  "1. Register Regioning patterns where register data bit location
       *      of the LSB of the channels are changed between source and
       *      destination are not supported on Src0 and Src1 except for
       *      broadcast of a scalar.
       *
       *   2. Explicit ARF registers except null and accumulator must not be
       *      used."
       */
      if (devinfo->verx10 >= 125 &&
          (brw_type_is_float(dst_type) ||
           is_double_precision)) {
         ERROR_IF(!is_scalar_region &&
                  BRW_ADDRESS_REGISTER_INDIRECT_REGISTER != address_mode &&
                  (!is_linear(vstride, width, hstride) ||
                   src_stride != dst_stride ||
                   subreg != dst_subreg),
                  "Register Regioning patterns where register data bit "
                  "location of the LSB of the channels are changed between "
                  "source and destination are not supported except for "
                  "broadcast of a scalar.");

         ERROR_IF((address_mode == BRW_ADDRESS_DIRECT && file == ARF &&
                   reg != BRW_ARF_NULL && !(reg >= BRW_ARF_ACCUMULATOR && reg < BRW_ARF_FLAG)) ||
                  (inst->dst.file == ARF &&
                   dst_reg != BRW_ARF_NULL && (dst_reg & 0xF0) != BRW_ARF_ACCUMULATOR),
                  "Explicit ARF registers except null and accumulator must not "
                  "be used.");
      }

      /* From the hardware spec section "Register Region Restrictions":
       *
       * "Vx1 and VxH indirect addressing for Float, Half-Float, Double-Float and
       *  Quad-Word data must not be used."
       */
      if (devinfo->verx10 >= 125 &&
          (brw_type_is_float(type) || brw_type_size_bytes(type) == 8)) {
         ERROR_IF(address_mode == BRW_ADDRESS_REGISTER_INDIRECT_REGISTER &&
                  vstride == BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL,
                  "Vx1 and VxH indirect addressing for Float, Half-Float, "
                  "Double-Float and Quad-Word data must not be used");
      }
   }

   /* The PRMs say that for BDW, SKL:
    *
    *    If Align16 is required for an operation with QW destination and non-QW
    *    source datatypes, the execution size cannot exceed 2.
    *
    * We assume that the restriction applies to all Gfx8+ parts.
    */
   if (is_double_precision) {
      enum brw_reg_type src0_type = inst->src[0].type;
      enum brw_reg_type src1_type =
         inst->num_sources > 1 ? inst->src[1].type : src0_type;
      unsigned src0_type_size = brw_type_size_bytes(src0_type);
      unsigned src1_type_size = brw_type_size_bytes(src1_type);

      ERROR_IF(inst->access_mode == BRW_ALIGN_16 &&
               dst_type_size == 8 &&
               (src0_type_size != 8 || src1_type_size != 8) &&
               inst->exec_size > 2,
               "In Align16 exec size cannot exceed 2 with a QWord destination "
               "and a non-QWord source");
   }

   /* The PRMs say that for CHV, BXT:
    *
    *    When source or destination datatype is 64b or operation is integer
    *    DWord multiply, DepCtrl must not be used.
    *
    * We assume that the restriction applies to GLK as well.
    */
   if (is_double_precision &&
       intel_device_info_is_9lp(devinfo)) {
      ERROR_IF(brw_inst_no_dd_check(devinfo, inst->raw) ||
               brw_inst_no_dd_clear(devinfo, inst->raw),
               "DepCtrl is not allowed when the execution type is 64-bit");
   }

   return error_msg;
}

static struct string
instruction_restrictions(const struct brw_isa_info *isa,
                         const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   struct string error_msg = { .str = NULL, .len = 0 };

   /* From Wa_1604601757:
    *
    * "When multiplying a DW and any lower precision integer, source modifier
    *  is not supported."
    */
   if (devinfo->ver >= 12 &&
       inst->opcode == BRW_OPCODE_MUL) {
      enum brw_reg_type exec_type = execution_type(inst);
      const bool src0_valid =
         brw_type_size_bytes(inst->src[0].type) == 4 ||
         inst->src[0].file == IMM ||
         !(inst->src[0].negate || inst->src[0].abs);
      const bool src1_valid =
         brw_type_size_bytes(inst->src[1].type) == 4 ||
         inst->src[1].file == IMM ||
         !(inst->src[1].negate || inst->src[1].abs);

      ERROR_IF(!brw_type_is_float(exec_type) &&
               brw_type_size_bytes(exec_type) == 4 &&
               !(src0_valid && src1_valid),
               "When multiplying a DW and any lower precision integer, source "
               "modifier is not supported.");
   }

   if (inst->opcode == BRW_OPCODE_CMP ||
       inst->opcode == BRW_OPCODE_CMPN) {
      ERROR_IF(inst->cond_modifier == BRW_CONDITIONAL_NONE,
               "CMP (or CMPN) must have a condition.");
   }

   if (inst->opcode == BRW_OPCODE_SEL) {
      ERROR_IF((inst->cond_modifier != BRW_CONDITIONAL_NONE) ==
               (inst->pred_control != BRW_PREDICATE_NONE),
               "SEL must either be predicated or have a condition modifiers");
   }

   if (inst->opcode == BRW_OPCODE_MUL) {
      const enum brw_reg_type src0_type = inst->src[0].type;
      const enum brw_reg_type src1_type = inst->src[1].type;
      const enum brw_reg_type dst_type = inst->dst.type;

      /* Page 966 (page 982 of the PDF) of Broadwell PRM volume 2a says:
       *
       *    When multiplying a DW and any lower precision integer, the DW
       *    operand must on src0.
       *
       * Ivy Bridge, Haswell, Skylake, and Ice Lake PRMs contain the same
       * text.
       */
      ERROR_IF(brw_type_is_int(src1_type) &&
               brw_type_size_bytes(src0_type) < 4 &&
               brw_type_size_bytes(src1_type) == 4,
               "When multiplying a DW and any lower precision integer, the "
               "DW operand must be src0.");

      /* Page 971 (page 987 of the PDF), section "Accumulator
       * Restrictions," of the Broadwell PRM volume 7 says:
       *
       *    Integer source operands cannot be accumulators.
       *
       * The Skylake and Ice Lake PRMs contain the same text.
       */
      ERROR_IF((src0_is_acc(inst) &&
                brw_type_is_int(src0_type)) ||
               (src1_is_acc(inst) &&
                brw_type_is_int(src1_type)),
               "Integer source operands cannot be accumulators.");

      /* Page 935 (page 951 of the PDF) of the Ice Lake PRM volume 2a says:
       *
       *    When multiplying integer data types, if one of the sources is a
       *    DW, the resulting full precision data is stored in the
       *    accumulator. However, if the destination data type is either W or
       *    DW, the low bits of the result are written to the destination
       *    register and the remaining high bits are discarded. This results
       *    in undefined Overflow and Sign flags. Therefore, conditional
       *    modifiers and saturation (.sat) cannot be used in this case.
       *
       * Similar text appears in every version of the PRM.
       *
       * The wording of the last sentence is not very clear.  It could either
       * be interpreted as "conditional modifiers combined with saturation
       * cannot be used" or "neither conditional modifiers nor saturation can
       * be used."  I have interpreted it as the latter primarily because that
       * is the more restrictive interpretation.
       */
      ERROR_IF((src0_type == BRW_TYPE_UD ||
                src0_type == BRW_TYPE_D ||
                src1_type == BRW_TYPE_UD ||
                src1_type == BRW_TYPE_D) &&
               (dst_type == BRW_TYPE_UD ||
                dst_type == BRW_TYPE_D ||
                dst_type == BRW_TYPE_UW ||
                dst_type == BRW_TYPE_W) &&
               (inst->saturate || inst->cond_modifier != BRW_CONDITIONAL_NONE),
               "Neither Saturate nor conditional modifier allowed with DW "
               "integer multiply.");
   }

   if (inst->opcode == BRW_OPCODE_MATH) {
      unsigned math_function = brw_inst_math_function(devinfo, inst->raw);
      switch (math_function) {
      case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER:
      case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT:
      case BRW_MATH_FUNCTION_INT_DIV_REMAINDER: {
         /* Page 442 of the Broadwell PRM Volume 2a "Extended Math Function" says:
          *    INT DIV function does not support source modifiers.
          * Bspec 6647 extends it back to Ivy Bridge.
          */
         bool src0_valid = !inst->src[0].negate && !inst->src[0].abs;
         bool src1_valid = !inst->src[1].negate && !inst->src[1].abs;
         ERROR_IF(!src0_valid || !src1_valid,
                  "INT DIV function does not support source modifiers.");
         break;
      }
      default:
         break;
      }
   }

   if (inst->opcode == BRW_OPCODE_DP4A) {
      /* Page 396 (page 412 of the PDF) of the DG1 PRM volume 2a says:
       *
       *    Only one of src0 or src1 operand may be an the (sic) accumulator
       *    register (acc#).
       */
      ERROR_IF(src0_is_acc(inst) && src1_is_acc(inst),
               "Only one of src0 or src1 operand may be an accumulator "
               "register (acc#).");

   }

   if (inst->opcode == BRW_OPCODE_ADD3) {
      const enum brw_reg_type dst_type = inst->dst.type;

      ERROR_IF(dst_type != BRW_TYPE_D &&
               dst_type != BRW_TYPE_UD &&
               dst_type != BRW_TYPE_W &&
               dst_type != BRW_TYPE_UW,
               "Destination must be integer D, UD, W, or UW type.");

      for (unsigned i = 0; i < 3; i++) {
         enum brw_reg_type src_type = inst->src[i].type;

         ERROR_IF(src_type != BRW_TYPE_D &&
                  src_type != BRW_TYPE_UD &&
                  src_type != BRW_TYPE_W &&
                  src_type != BRW_TYPE_UW,
                  "Source must be integer D, UD, W, or UW type.");

         ERROR_IF(inst->src[i].file == IMM &&
                  src_type != BRW_TYPE_W &&
                  src_type != BRW_TYPE_UW,
                  "Immediate source must be integer W or UW type.");
      }
   }

   if (inst->opcode == BRW_OPCODE_OR ||
       inst->opcode == BRW_OPCODE_AND ||
       inst->opcode == BRW_OPCODE_XOR ||
       inst->opcode == BRW_OPCODE_NOT) {
      /* While the behavior of the negate source modifier is defined as
       * logical not, the behavior of abs source modifier is not
       * defined. Disallow it to be safe.
       */
      ERROR_IF(inst->src[0].abs,
               "Behavior of abs source modifier in logic ops is undefined.");
      ERROR_IF(inst->opcode != BRW_OPCODE_NOT &&
               inst->src[1].file != IMM &&
               inst->src[1].abs,
               "Behavior of abs source modifier in logic ops is undefined.");

      /* Page 479 (page 495 of the PDF) of the Broadwell PRM volume 2a says:
       *
       *    Source modifier is not allowed if source is an accumulator.
       *
       * The same text also appears for OR, NOT, and XOR instructions.
       */
      ERROR_IF((inst->src[0].abs || inst->src[0].negate) &&
               src0_is_acc(inst),
               "Source modifier is not allowed if source is an accumulator.");
      ERROR_IF(inst->num_sources > 1 &&
               (inst->src[1].abs || inst->src[1].negate) &&
               src1_is_acc(inst),
               "Source modifier is not allowed if source is an accumulator.");

      /* Page 479 (page 495 of the PDF) of the Broadwell PRM volume 2a says:
       *
       *    This operation does not produce sign or overflow conditions. Only
       *    the .e/.z or .ne/.nz conditional modifiers should be used.
       *
       * The same text also appears for OR, NOT, and XOR instructions.
       *
       * Per the comment around nir_op_imod in brw_fs_nir.cpp, we have
       * determined this to not be true. The only conditions that seem
       * absolutely sketchy are O, R, and U.  Some OpenGL shaders from Doom
       * 2016 have been observed to generate and.g and operate correctly.
       */
      const enum brw_conditional_mod cmod = inst->cond_modifier;
      ERROR_IF(cmod == BRW_CONDITIONAL_O ||
               cmod == BRW_CONDITIONAL_R ||
               cmod == BRW_CONDITIONAL_U,
               "O, R, and U conditional modifiers should not be used.");
   }

   if (inst->opcode == BRW_OPCODE_BFI2) {
      ERROR_IF(inst->cond_modifier != BRW_CONDITIONAL_NONE,
               "BFI2 cannot have conditional modifier");

      ERROR_IF(inst->saturate,
               "BFI2 cannot have saturate modifier");

      enum brw_reg_type dst_type = inst->dst.type;

      ERROR_IF(dst_type != BRW_TYPE_D &&
               dst_type != BRW_TYPE_UD,
               "BFI2 destination type must be D or UD");

      for (unsigned s = 0; s < 3; s++) {
         enum brw_reg_type src_type = inst->src[s].type;

         ERROR_IF(src_type != dst_type,
                  "BFI2 source type must match destination type");
      }
   }

   if (inst->opcode == BRW_OPCODE_CSEL) {
      ERROR_IF(inst->pred_control != BRW_PREDICATE_NONE,
               "CSEL cannot be predicated");

      /* CSEL is CMP and SEL fused into one. The condition modifier, which
       * does not actually modify the flags, controls the built-in comparison.
       */
      ERROR_IF(inst->cond_modifier == BRW_CONDITIONAL_NONE,
               "CSEL must have a condition.");

      enum brw_reg_type dst_type = inst->dst.type;

      if (devinfo->ver == 9) {
         ERROR_IF(dst_type != BRW_TYPE_F,
                  "CSEL destination type must be F");
      } else {
         ERROR_IF(dst_type != BRW_TYPE_F &&
                  dst_type != BRW_TYPE_HF &&
                  dst_type != BRW_TYPE_D &&
                  dst_type != BRW_TYPE_W &&
                  dst_type != BRW_TYPE_UD &&
                  dst_type != BRW_TYPE_UW,
                  "CSEL destination type must be F, HF, *D, or *W");
      }

      for (unsigned s = 0; s < 3; s++) {
         enum brw_reg_type src_type = inst->src[s].type;

         if (devinfo->ver == 9) {
            ERROR_IF(src_type != BRW_TYPE_F,
                     "CSEL source type must be F");
         } else {
            ERROR_IF(src_type != BRW_TYPE_F && src_type != BRW_TYPE_HF &&
                     src_type != BRW_TYPE_D && src_type != BRW_TYPE_UD &&
                     src_type != BRW_TYPE_W && src_type != BRW_TYPE_UW,
                     "CSEL source type must be F, HF, *D, or *W");

            ERROR_IF(brw_type_is_float(src_type) != brw_type_is_float(dst_type),
                     "CSEL cannot mix float and integer types.");

            ERROR_IF(brw_type_size_bytes(src_type) !=
                     brw_type_size_bytes(dst_type),
                     "CSEL cannot mix different type sizes.");
         }
      }
   }

   if (inst->opcode == BRW_OPCODE_DPAS) {
      ERROR_IF(brw_inst_dpas_3src_sdepth(devinfo, inst->raw) != BRW_SYSTOLIC_DEPTH_8,
               "Systolic depth must be 8.");

      const unsigned sdepth = 8;

      const enum brw_reg_type dst_type = inst->dst.type;
      const enum brw_reg_type src0_type = inst->src[0].type;
      const enum brw_reg_type src1_type = inst->src[1].type;
      const enum brw_reg_type src2_type = inst->src[2].type;

      const enum gfx12_sub_byte_precision src1_sub_byte =
         brw_inst_dpas_3src_src1_subbyte(devinfo, inst->raw);

      if (src1_type != BRW_TYPE_B && src1_type != BRW_TYPE_UB) {
         ERROR_IF(src1_sub_byte != BRW_SUB_BYTE_PRECISION_NONE,
                  "Sub-byte precision must be None for source type larger than Byte.");
      } else {
         ERROR_IF(src1_sub_byte != BRW_SUB_BYTE_PRECISION_NONE &&
                  src1_sub_byte != BRW_SUB_BYTE_PRECISION_4BIT &&
                  src1_sub_byte != BRW_SUB_BYTE_PRECISION_2BIT,
                  "Invalid sub-byte precision.");
      }

      const enum gfx12_sub_byte_precision src2_sub_byte =
         brw_inst_dpas_3src_src2_subbyte(devinfo, inst->raw);

      if (src2_type != BRW_TYPE_B && src2_type != BRW_TYPE_UB) {
         ERROR_IF(src2_sub_byte != BRW_SUB_BYTE_PRECISION_NONE,
                  "Sub-byte precision must be None.");
      } else {
         ERROR_IF(src2_sub_byte != BRW_SUB_BYTE_PRECISION_NONE &&
                  src2_sub_byte != BRW_SUB_BYTE_PRECISION_4BIT &&
                  src2_sub_byte != BRW_SUB_BYTE_PRECISION_2BIT,
                  "Invalid sub-byte precision.");
      }

      const unsigned src1_bits_per_element =
         brw_type_size_bits(src1_type) >>
         brw_inst_dpas_3src_src1_subbyte(devinfo, inst->raw);

      const unsigned src2_bits_per_element =
         brw_type_size_bits(src2_type) >>
         brw_inst_dpas_3src_src2_subbyte(devinfo, inst->raw);

      /* The MAX2(1, ...) is just to prevent possible division by 0 later. */
      const unsigned ops_per_chan =
         MAX2(1, 32 / MAX2(src1_bits_per_element, src2_bits_per_element));

      if (devinfo->ver < 20) {
         ERROR_IF(inst->exec_size != 8, "DPAS execution size must be 8.");
      } else {
         ERROR_IF(inst->exec_size != 16, "DPAS execution size must be 16.");
      }

      const unsigned dst_subnr  = inst->dst.subnr;
      const unsigned src0_subnr = inst->src[0].subnr;
      const unsigned src1_subnr = inst->src[1].subnr;
      const unsigned src2_subnr = inst->src[2].subnr;

      /* Until HF is supported as dst type, this is effectively subnr == 0. */
      ERROR_IF(dst_subnr % inst->exec_size != 0,
               "Destination subregister offset must be a multiple of ExecSize.");

      /* Until HF is supported as src0 type, this is effectively subnr == 0. */
      ERROR_IF(src0_subnr % inst->exec_size != 0,
               "Src0 subregister offset must be a multiple of ExecSize.");

      ERROR_IF(src1_subnr != 0,
               "Src1 subregister offsets must be 0.");

      /* In nearly all cases, this effectively requires that src2.subnr be
       * 0. It is only when src1 is 8 bits and src2 is 2 or 4 bits that the
       * ops_per_chan value can allow non-zero src2.subnr.
       */
      ERROR_IF(src2_subnr % (sdepth * ops_per_chan) != 0,
               "Src2 subregister offset must be a multiple of SystolicDepth "
               "times OPS_PER_CHAN.");

      ERROR_IF(dst_subnr * brw_type_size_bytes(dst_type) >= REG_SIZE,
               "Destination subregister specifies next register.");

      ERROR_IF(src0_subnr * brw_type_size_bytes(src0_type) >= REG_SIZE,
               "Src0 subregister specifies next register.");

      ERROR_IF((src1_subnr * brw_type_size_bytes(src1_type) * src1_bits_per_element) / 8 >= REG_SIZE,
               "Src1 subregister specifies next register.");

      ERROR_IF((src2_subnr * brw_type_size_bytes(src2_type) * src2_bits_per_element) / 8 >= REG_SIZE,
               "Src2 subregister specifies next register.");

      if (brw_inst_3src_atomic_control(devinfo, inst->raw)) {
         /* FINISHME: When we start emitting DPAS with Atomic set, figure out
          * a way to validate it. Also add a test in test_eu_validate.cpp.
          */
         ERROR_IF(true,
                  "When instruction option Atomic is used it must be follwed by a "
                  "DPAS instruction.");
      }

      if (brw_inst_dpas_3src_exec_type(devinfo, inst->raw) ==
          BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT) {
         ERROR_IF(dst_type != BRW_TYPE_F,
                  "DPAS destination type must be F.");
         ERROR_IF(src0_type != BRW_TYPE_F,
                  "DPAS src0 type must be F.");
         ERROR_IF(src1_type != BRW_TYPE_HF,
                  "DPAS src1 type must be HF.");
         ERROR_IF(src2_type != BRW_TYPE_HF,
                  "DPAS src2 type must be HF.");
      } else {
         ERROR_IF(dst_type != BRW_TYPE_D &&
                  dst_type != BRW_TYPE_UD,
                  "DPAS destination type must be D or UD.");
         ERROR_IF(src0_type != BRW_TYPE_D &&
                  src0_type != BRW_TYPE_UD,
                  "DPAS src0 type must be D or UD.");
         ERROR_IF(src1_type != BRW_TYPE_B &&
                  src1_type != BRW_TYPE_UB,
                  "DPAS src1 base type must be B or UB.");
         ERROR_IF(src2_type != BRW_TYPE_B &&
                  src2_type != BRW_TYPE_UB,
                  "DPAS src2 base type must be B or UB.");

         if (brw_type_is_uint(dst_type)) {
            ERROR_IF(!brw_type_is_uint(src0_type) ||
                     !brw_type_is_uint(src1_type) ||
                     !brw_type_is_uint(src2_type),
                     "If any source datatype is signed, destination datatype "
                     "must be signed.");
         }
      }

      /* FINISHME: Additional restrictions mentioned in the Bspec that are not
       * yet enforced here:
       *
       *    - General Accumulator registers access is not supported. This is
       *      currently enforced in brw_dpas_three_src (brw_eu_emit.c).
       *
       *    - Given any combination of datatypes in the sources of a DPAS
       *      instructions, the boundaries of a register should not be crossed.
       */
   }

   return error_msg;
}

static struct string
send_descriptor_restrictions(const struct brw_isa_info *isa,
                             const brw_hw_decoded_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   struct string error_msg = { .str = NULL, .len = 0 };

   if (inst_is_split_send(isa, inst)) {
      /* We can only validate immediate descriptors */
      if (brw_inst_send_sel_reg32_desc(devinfo, inst->raw))
         return error_msg;
   } else if (inst_is_send(inst)) {
      /* We can only validate immediate descriptors */
      if (inst->src[1].file != IMM)
         return error_msg;
   } else {
      return error_msg;
   }

   const uint32_t desc = brw_inst_send_desc(devinfo, inst->raw);

   switch (brw_inst_sfid(devinfo, inst->raw)) {
   case BRW_SFID_URB:
      if (devinfo->ver < 20)
         break;
      FALLTHROUGH;
   case GFX12_SFID_TGM:
   case GFX12_SFID_SLM:
   case GFX12_SFID_UGM:
      ERROR_IF(!devinfo->has_lsc, "Platform does not support LSC");

      ERROR_IF(lsc_opcode_has_transpose(lsc_msg_desc_opcode(devinfo, desc)) &&
               lsc_msg_desc_transpose(devinfo, desc) &&
               inst->exec_size != 1,
               "Transposed vectors are restricted to Exec_Mask = 1.");
      break;

   default:
      break;
   }

   if (brw_inst_sfid(devinfo, inst->raw) == BRW_SFID_URB && devinfo->ver < 20) {
      ERROR_IF(!brw_inst_header_present(devinfo, inst->raw),
               "Header must be present for all URB messages.");

      switch (brw_inst_urb_opcode(devinfo, inst->raw)) {
      case GFX7_URB_OPCODE_ATOMIC_INC:
      case GFX7_URB_OPCODE_ATOMIC_MOV:
      case GFX8_URB_OPCODE_ATOMIC_ADD:
      case GFX8_URB_OPCODE_SIMD8_WRITE:
         break;

      case GFX8_URB_OPCODE_SIMD8_READ:
         ERROR_IF(brw_inst_rlen(devinfo, inst->raw) == 0,
                  "URB SIMD8 read message must read some data.");
         break;

      case GFX125_URB_OPCODE_FENCE:
         ERROR_IF(devinfo->verx10 < 125,
                  "URB fence message only valid on gfx >= 12.5");
         break;

      default:
         ERROR_IF(true, "Invalid URB message");
         break;
      }
   }

   return error_msg;
}

static unsigned
VSTRIDE_3SRC(unsigned vstride)
{
   switch (vstride) {
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_0: return 0;
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_1: return 1;
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_4: return 4;
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_8: return 8;
   }
   unreachable("invalid vstride");
}

static struct string
brw_hw_decode_inst(const struct brw_isa_info *isa,
                   brw_hw_decoded_inst *inst,
                   const brw_inst *raw)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   struct string error_msg = { .str = NULL, .len = 0 };

   inst->raw = raw;
   inst->opcode = brw_inst_opcode(isa, raw);
   inst->num_sources = brw_num_sources_from_inst(isa, raw);

   const struct opcode_desc *desc = brw_opcode_desc(isa, inst->opcode);
   assert(desc->ndst == 0 || desc->ndst == 1);
   inst->has_dst = desc->ndst == 1;

   enum brw_execution_size exec_size = brw_inst_exec_size(devinfo, raw);
   switch (exec_size) {
   case BRW_EXECUTE_1:
   case BRW_EXECUTE_2:
   case BRW_EXECUTE_4:
   case BRW_EXECUTE_8:
   case BRW_EXECUTE_16:
   case BRW_EXECUTE_32:
      inst->exec_size = 1 << exec_size;
      break;
   default:
      RETURN_ERROR("invalid execution size");
      break;
   }

   inst->access_mode = brw_inst_access_mode(devinfo, raw);
   inst->cond_modifier = brw_inst_cond_modifier(devinfo, raw);
   inst->pred_control = brw_inst_pred_control(devinfo, raw);
   inst->saturate = brw_inst_saturate(devinfo, raw);

   RETURN_ERROR_IF(inst->num_sources == 3 && inst->access_mode == BRW_ALIGN_1 && devinfo->ver == 9,
                   "Align1 mode not allowed on Gfx9 for 3-src instructions");

   RETURN_ERROR_IF(inst->access_mode == BRW_ALIGN_16 && devinfo->ver >= 11,
                   "Align16 mode doesn't exist on Gfx11+");

   enum instr_format {
      FORMAT_BASIC,
      FORMAT_BASIC_THREE_SRC,
      FORMAT_DPAS_THREE_SRC,
      FORMAT_SEND,
      FORMAT_BRANCH,
      FORMAT_ILLEGAL,
      FORMAT_NOP,
   };

   enum instr_format format;
   switch (inst->opcode) {
   case BRW_OPCODE_DPAS:
      format = FORMAT_DPAS_THREE_SRC;
      break;

   case BRW_OPCODE_SEND:
   case BRW_OPCODE_SENDC:
      format = devinfo->ver >= 12 ? FORMAT_SEND : FORMAT_BASIC;
      break;

   case BRW_OPCODE_SENDS:
   case BRW_OPCODE_SENDSC:
      format = FORMAT_SEND;
      break;

   case BRW_OPCODE_DO:
   case BRW_OPCODE_WHILE:
   case BRW_OPCODE_IF:
   case BRW_OPCODE_ELSE:
   case BRW_OPCODE_ENDIF:
   case BRW_OPCODE_BREAK:
   case BRW_OPCODE_CONTINUE:
   case BRW_OPCODE_JMPI:
   case BRW_OPCODE_BRD:
   case BRW_OPCODE_BRC:
   case BRW_OPCODE_HALT:
   case BRW_OPCODE_CALLA:
   case BRW_OPCODE_CALL:
   case BRW_OPCODE_GOTO:
      format = FORMAT_BRANCH;
      break;

   case BRW_OPCODE_NOP:
      format = FORMAT_NOP;
      break;

   case BRW_OPCODE_ILLEGAL:
      format = FORMAT_ILLEGAL;
      break;

   default:
      if (inst->num_sources == 3) {
         format = FORMAT_BASIC_THREE_SRC;
      } else {
         format = FORMAT_BASIC;
      }
      break;
   }

   switch (format) {
   case FORMAT_BASIC: {
      assert(inst->num_sources == 1 ||
             inst->num_sources == 2 ||
             inst->opcode == BRW_OPCODE_WAIT);
      assert(inst->has_dst ||
             inst->opcode == BRW_OPCODE_SYNC);

      if (inst->has_dst) {
         inst->dst.file = brw_inst_dst_reg_file(devinfo, raw);
         inst->dst.type = brw_inst_dst_type(devinfo, raw);
         inst->dst.address_mode = brw_inst_dst_address_mode(devinfo, raw);
         if (inst->dst.address_mode == BRW_ADDRESS_DIRECT) {
            inst->dst.nr = brw_inst_dst_da_reg_nr(devinfo, raw);
            if (inst->access_mode == BRW_ALIGN_1) {
               inst->dst.subnr = brw_inst_dst_da1_subreg_nr(devinfo, raw);
            } else {
               inst->dst.subnr = brw_inst_dst_da16_subreg_nr(devinfo, raw);
            }
         } else {
            inst->dst.subnr = brw_inst_dst_ia_subreg_nr(devinfo, raw);
         }
         inst->dst.hstride = STRIDE(brw_inst_dst_hstride(devinfo, raw));
      }

      inst->src[0].file = brw_inst_src0_reg_file(devinfo, raw);
      inst->src[0].type = brw_inst_src0_type(devinfo, raw);
      inst->src[0].address_mode = brw_inst_src0_address_mode(devinfo, raw);
      inst->src[0].negate = brw_inst_src0_negate(devinfo, raw);
      inst->src[0].abs = brw_inst_src0_abs(devinfo, raw);
      if (inst->src[0].file != IMM) {
         if (inst->src[0].address_mode == BRW_ADDRESS_DIRECT) {
            inst->src[0].nr = brw_inst_src0_da_reg_nr(devinfo, raw);
            if (inst->access_mode == BRW_ALIGN_1) {
               inst->src[0].subnr = brw_inst_src0_da1_subreg_nr(devinfo, raw);
            } else {
               inst->src[0].subnr = brw_inst_src0_da16_subreg_nr(devinfo, raw) * 16;
            }
         } else {
            inst->src[0].subnr = brw_inst_src0_ia_subreg_nr(devinfo, raw);
         }

         inst->src[0].vstride = STRIDE(brw_inst_src0_vstride(devinfo, raw));
         if (inst->access_mode == BRW_ALIGN_1) {
            inst->src[0].width = WIDTH(brw_inst_src0_width(devinfo, raw));
            inst->src[0].hstride = STRIDE(brw_inst_src0_hstride(devinfo, raw));
         }
      }

      if (inst->num_sources > 1) {
         inst->src[1].file = brw_inst_src1_reg_file(devinfo, raw);
         inst->src[1].type = brw_inst_src1_type(devinfo, raw);
         inst->src[1].negate = brw_inst_src1_negate(devinfo, raw);
         inst->src[1].abs = brw_inst_src1_abs(devinfo, raw);
         if (inst->src[1].file != IMM) {
            if (inst->src[1].address_mode == BRW_ADDRESS_DIRECT) {
               inst->src[1].nr = brw_inst_src1_da_reg_nr(devinfo, raw);
               if (inst->access_mode == BRW_ALIGN_1) {
                  inst->src[1].subnr = brw_inst_src1_da1_subreg_nr(devinfo, raw);
               } else {
                  inst->src[1].subnr = brw_inst_src1_da16_subreg_nr(devinfo, raw) * 16;
               }
            } else {
               inst->src[1].subnr = brw_inst_src1_ia_subreg_nr(devinfo, raw);
            }

            inst->src[1].vstride = STRIDE(brw_inst_src1_vstride(devinfo, raw));
            if (inst->access_mode == BRW_ALIGN_1) {
               inst->src[1].width = WIDTH(brw_inst_src1_width(devinfo, raw));
               inst->src[1].hstride = STRIDE(brw_inst_src1_hstride(devinfo, raw));
            }
         }
      }

      break;
   }

   case FORMAT_BASIC_THREE_SRC: {
      assert(inst->num_sources == 3);
      assert(inst->has_dst);

      if (inst->access_mode == BRW_ALIGN_1) {
         inst->dst.file = brw_inst_3src_a1_dst_reg_file(devinfo, raw);
         inst->dst.type = brw_inst_3src_a1_dst_type(devinfo, raw);
         inst->dst.nr = brw_inst_3src_dst_reg_nr(devinfo, raw);
         inst->dst.subnr = brw_inst_3src_a1_dst_subreg_nr(devinfo, raw) * 8;
         inst->dst.hstride = STRIDE(brw_inst_3src_a1_dst_hstride(devinfo, raw));

         inst->src[0].file = brw_inst_3src_a1_src0_reg_file(devinfo, raw);
         inst->src[0].type = brw_inst_3src_a1_src0_type(devinfo, raw);
         inst->src[0].negate = brw_inst_3src_src0_negate(devinfo, raw);
         inst->src[0].abs = brw_inst_3src_src0_abs(devinfo, raw);
         if (inst->src[0].file != IMM) {
            inst->src[0].nr = brw_inst_3src_src0_reg_nr(devinfo, raw);
            inst->src[0].subnr = brw_inst_3src_a1_src0_subreg_nr(devinfo, raw);
            inst->src[0].vstride = VSTRIDE_3SRC(brw_inst_3src_a1_src0_vstride(devinfo, raw));
            inst->src[0].hstride = STRIDE(brw_inst_3src_a1_src0_hstride(devinfo, raw));
         }

         inst->src[1].file = brw_inst_3src_a1_src1_reg_file(devinfo, raw);
         inst->src[1].type = brw_inst_3src_a1_src1_type(devinfo, raw);
         inst->src[1].negate = brw_inst_3src_src1_negate(devinfo, raw);
         inst->src[1].abs = brw_inst_3src_src1_abs(devinfo, raw);
         inst->src[1].nr = brw_inst_3src_src1_reg_nr(devinfo, raw);
         inst->src[1].subnr = brw_inst_3src_a1_src1_subreg_nr(devinfo, raw);
         inst->src[1].vstride = VSTRIDE_3SRC(brw_inst_3src_a1_src1_vstride(devinfo, raw));
         inst->src[1].hstride = STRIDE(brw_inst_3src_a1_src1_hstride(devinfo, raw));

         inst->src[2].file = brw_inst_3src_a1_src2_reg_file(devinfo, raw);
         inst->src[2].type = brw_inst_3src_a1_src2_type(devinfo, raw);
         inst->src[2].negate = brw_inst_3src_src2_negate(devinfo, raw);
         inst->src[2].abs = brw_inst_3src_src2_abs(devinfo, raw);
         if (inst->src[2].file != IMM) {
            inst->src[2].nr = brw_inst_3src_src2_reg_nr(devinfo, raw);
            inst->src[2].subnr = brw_inst_3src_a1_src2_subreg_nr(devinfo, raw);
            inst->src[2].hstride = STRIDE(brw_inst_3src_a1_src2_hstride(devinfo, raw));
         }

      } else {
         inst->dst.file = FIXED_GRF;
         inst->dst.type = brw_inst_3src_a16_dst_type(devinfo, raw);
         inst->dst.nr = brw_inst_3src_dst_reg_nr(devinfo, raw);
         inst->dst.subnr = brw_inst_3src_a16_dst_subreg_nr(devinfo, raw) * 4;

         enum brw_reg_type src_type = brw_inst_3src_a16_src_type(devinfo, raw);

         inst->src[0].file = FIXED_GRF;
         inst->src[0].type = src_type;
         inst->src[0].nr = brw_inst_3src_src0_reg_nr(devinfo, raw);
         inst->src[0].subnr = brw_inst_3src_a16_src0_subreg_nr(devinfo, raw) * 4;

         inst->src[1].file = FIXED_GRF;
         inst->src[1].type = src_type;
         inst->src[1].nr = brw_inst_3src_src1_reg_nr(devinfo, raw);
         inst->src[1].subnr = brw_inst_3src_a16_src1_subreg_nr(devinfo, raw) * 4;

         inst->src[2].file = FIXED_GRF;
         inst->src[2].type = src_type;
         inst->src[2].nr = brw_inst_3src_src2_reg_nr(devinfo, raw);
         inst->src[2].subnr = brw_inst_3src_a16_src2_subreg_nr(devinfo, raw) * 4;
      }
      break;
   }

   case FORMAT_DPAS_THREE_SRC: {
      assert(inst->num_sources == 3);
      assert(inst->has_dst);

      inst->dst.file = brw_inst_dpas_3src_dst_reg_file(devinfo, raw);
      inst->dst.type = brw_inst_dpas_3src_dst_type(devinfo, raw);
      inst->dst.nr = brw_inst_dpas_3src_dst_reg_nr(devinfo, raw);
      inst->dst.subnr = brw_inst_dpas_3src_dst_subreg_nr(devinfo, raw);

      inst->src[0].file = brw_inst_dpas_3src_src0_reg_file(devinfo, raw);
      inst->src[0].type = brw_inst_dpas_3src_src0_type(devinfo, raw);
      inst->src[0].nr = brw_inst_dpas_3src_src0_reg_nr(devinfo, raw);
      inst->src[0].subnr = brw_inst_dpas_3src_src0_subreg_nr(devinfo, raw);

      inst->src[1].file = brw_inst_dpas_3src_src1_reg_file(devinfo, raw);
      inst->src[1].type = brw_inst_dpas_3src_src1_type(devinfo, raw);
      inst->src[1].nr = brw_inst_dpas_3src_src1_reg_nr(devinfo, raw);
      inst->src[1].subnr = brw_inst_dpas_3src_src1_subreg_nr(devinfo, raw);

      inst->src[2].file = brw_inst_dpas_3src_src2_reg_file(devinfo, raw);
      inst->src[2].type = brw_inst_dpas_3src_src2_type(devinfo, raw);
      inst->src[2].nr = brw_inst_dpas_3src_src2_reg_nr(devinfo, raw);
      inst->src[2].subnr = brw_inst_dpas_3src_src2_subreg_nr(devinfo, raw);
      break;
   }

   case FORMAT_SEND: {
      if (inst->opcode == BRW_OPCODE_SENDS || inst->opcode == BRW_OPCODE_SENDSC) {
         assert(devinfo->ver < 12);

         inst->dst.file = brw_inst_send_dst_reg_file(devinfo, raw);
         inst->dst.type = BRW_TYPE_D;
         inst->dst.nr = brw_inst_dst_da_reg_nr(devinfo, raw);
         inst->dst.subnr = brw_inst_dst_da16_subreg_nr(devinfo, raw) * 16;

         inst->src[0].file = FIXED_GRF;
         inst->src[0].type = BRW_TYPE_D;
         inst->src[0].nr = brw_inst_src0_da_reg_nr(devinfo, raw);
         inst->src[0].subnr = brw_inst_src0_da16_subreg_nr(devinfo, raw) * 16;

         if (inst->num_sources > 1) {
            inst->src[1].file = brw_inst_send_src1_reg_file(devinfo, raw);
            inst->src[1].type = BRW_TYPE_D;
            inst->src[1].nr = brw_inst_send_src1_reg_nr(devinfo, raw);
         }
      } else {
         assert(devinfo->ver >= 12);

         inst->dst.file = brw_inst_dst_reg_file(devinfo, raw);
         inst->dst.type = BRW_TYPE_D;
         inst->dst.nr = brw_inst_dst_da_reg_nr(devinfo, raw);

         inst->src[0].file = brw_inst_send_src0_reg_file(devinfo, raw);
         inst->src[0].type = BRW_TYPE_D;
         inst->src[0].nr = brw_inst_src0_da_reg_nr(devinfo, raw);

         if (inst->num_sources > 1) {
            inst->src[1].file = brw_inst_send_src1_reg_file(devinfo, raw);
            inst->src[1].type = BRW_TYPE_D;
            inst->src[1].nr = brw_inst_send_src1_reg_nr(devinfo, raw);
         }
      }
      break;
   }

   case FORMAT_BRANCH: {
      assert(!inst->has_dst);
      break;
   }

   case FORMAT_ILLEGAL:
   case FORMAT_NOP: {
      assert(!inst->has_dst);
      assert(inst->num_sources == 0);
      break;
   }
   }

   if (inst->has_dst) {
      ERROR_IF(inst->dst.type == BRW_TYPE_INVALID,
               "Invalid destination register type encoding.");
   }

   for (unsigned i = 0; i < inst->num_sources; i++) {
      ERROR_IF(inst->src[i].type == BRW_TYPE_INVALID,
               "Invalid source register type encoding.");
   }

   return error_msg;
}

bool
brw_validate_instruction(const struct brw_isa_info *isa,
                         const brw_inst *inst, int offset,
                         unsigned inst_size,
                         struct disasm_info *disasm)
{
   struct string error_msg = { .str = NULL, .len = 0 };

   if (is_unsupported_inst(isa, inst)) {
      ERROR("Instruction not supported on this Gen");
   } else {
      brw_hw_decoded_inst decoded = {};
      error_msg = brw_hw_decode_inst(isa, &decoded, inst);

#define CHECK(func, args...)                             \
   do {                                                  \
      struct string __msg = func(isa, &decoded, ##args); \
      if (__msg.str) {                                   \
         cat(&error_msg, __msg);                         \
         free(__msg.str);                                \
      }                                                  \
   } while (0)

      if (error_msg.str == NULL)
         CHECK(invalid_values);

      if (error_msg.str == NULL) {
         CHECK(sources_not_null);
         CHECK(send_restrictions);
         CHECK(general_restrictions_based_on_operand_types);
         CHECK(general_restrictions_on_region_parameters);
         CHECK(special_restrictions_for_mixed_float_mode);
         CHECK(region_alignment_rules);
         CHECK(vector_immediate_restrictions);
         CHECK(special_requirements_for_handling_double_precision_data_types);
         CHECK(instruction_restrictions);
         CHECK(send_descriptor_restrictions);
      }

#undef CHECK
   }

   if (error_msg.str && disasm) {
      disasm_insert_error(disasm, offset, inst_size, error_msg.str);
   }
   free(error_msg.str);

   return error_msg.len == 0;
}

bool
brw_validate_instructions(const struct brw_isa_info *isa,
                          const void *assembly, int start_offset, int end_offset,
                          struct disasm_info *disasm)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   bool valid = true;

   for (int src_offset = start_offset; src_offset < end_offset;) {
      const brw_inst *inst = assembly + src_offset;
      bool is_compact = brw_inst_cmpt_control(devinfo, inst);
      unsigned inst_size = is_compact ? sizeof(brw_compact_inst)
                                      : sizeof(brw_inst);
      brw_inst uncompacted;

      if (is_compact) {
         brw_compact_inst *compacted = (void *)inst;
         brw_uncompact_instruction(isa, &uncompacted, compacted);
         inst = &uncompacted;
      }

      bool v = brw_validate_instruction(isa, inst, src_offset,
                                        inst_size, disasm);
      valid = valid && v;

      src_offset += inst_size;
   }

   return valid;
}
