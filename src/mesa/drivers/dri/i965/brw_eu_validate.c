/*
 * Copyright © 2015 Intel Corporation
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

/** @file brw_eu_validate.c
 *
 * This file implements a pass that validates shader assembly.
 */

#include "brw_eu.h"

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

#define error(str) "\tERROR: " str "\n"

#define ERROR_IF(cond, msg)          \
   do {                              \
      if (cond) {                    \
         CAT(error_msg, error(msg)); \
         valid = false;              \
      }                              \
   } while(0)

static bool
src0_is_null(const struct brw_device_info *devinfo, const brw_inst *inst)
{
   return brw_inst_src0_reg_file(devinfo, inst) == BRW_ARCHITECTURE_REGISTER_FILE &&
          brw_inst_src0_da_reg_nr(devinfo, inst) == BRW_ARF_NULL;
}

static bool
src1_is_null(const struct brw_device_info *devinfo, const brw_inst *inst)
{
   return brw_inst_src1_reg_file(devinfo, inst) == BRW_ARCHITECTURE_REGISTER_FILE &&
          brw_inst_src1_da_reg_nr(devinfo, inst) == BRW_ARF_NULL;
}

enum gen {
   GEN4  = (1 << 0),
   GEN45 = (1 << 1),
   GEN5  = (1 << 2),
   GEN6  = (1 << 3),
   GEN7  = (1 << 4),
   GEN75 = (1 << 5),
   GEN8  = (1 << 6),
   GEN9  = (1 << 7),
   GEN_ALL = ~0
};

#define GEN_GE(gen) (~((gen) - 1) | gen)
#define GEN_LE(gen) (((gen) - 1) | gen)

struct inst_info {
   enum gen gen;
};

static const struct inst_info inst_info[128] = {
   [BRW_OPCODE_ILLEGAL] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_MOV] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SEL] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_MOVI] = {
      .gen = GEN_GE(GEN45),
   },
   [BRW_OPCODE_NOT] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_AND] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_OR] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_XOR] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SHR] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SHL] = {
      .gen = GEN_ALL,
   },
   /* BRW_OPCODE_DIM / BRW_OPCODE_SMOV */
   /* Reserved - 11 */
   [BRW_OPCODE_ASR] = {
      .gen = GEN_ALL,
   },
   /* Reserved - 13-15 */
   [BRW_OPCODE_CMP] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_CMPN] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_CSEL] = {
      .gen = GEN_GE(GEN8),
   },
   [BRW_OPCODE_F32TO16] = {
      .gen = GEN7 | GEN75,
   },
   [BRW_OPCODE_F16TO32] = {
      .gen = GEN7 | GEN75,
   },
   /* Reserved - 21-22 */
   [BRW_OPCODE_BFREV] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_BFE] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_BFI1] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_BFI2] = {
      .gen = GEN_GE(GEN7),
   },
   /* Reserved - 27-31 */
   [BRW_OPCODE_JMPI] = {
      .gen = GEN_ALL,
   },
   /* BRW_OPCODE_BRD */
   [BRW_OPCODE_IF] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_IFF] = { /* also BRW_OPCODE_BRC */
      .gen = GEN_LE(GEN5),
   },
   [BRW_OPCODE_ELSE] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_ENDIF] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_DO] = { /* also BRW_OPCODE_CASE */
      .gen = GEN_LE(GEN5),
   },
   [BRW_OPCODE_WHILE] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_BREAK] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_CONTINUE] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_HALT] = {
      .gen = GEN_ALL,
   },
   /* BRW_OPCODE_CALLA */
   /* BRW_OPCODE_MSAVE / BRW_OPCODE_CALL */
   /* BRW_OPCODE_MREST / BRW_OPCODE_RET */
   /* BRW_OPCODE_PUSH / BRW_OPCODE_FORK / BRW_OPCODE_GOTO */
   /* BRW_OPCODE_POP */
   [BRW_OPCODE_WAIT] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SEND] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SENDC] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SENDS] = {
      .gen = GEN_GE(GEN9),
   },
   [BRW_OPCODE_SENDSC] = {
      .gen = GEN_GE(GEN9),
   },
   /* Reserved 53-55 */
   [BRW_OPCODE_MATH] = {
      .gen = GEN_GE(GEN6),
   },
   /* Reserved 57-63 */
   [BRW_OPCODE_ADD] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_MUL] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_AVG] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_FRC] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_RNDU] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_RNDD] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_RNDE] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_RNDZ] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_MAC] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_MACH] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_LZD] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_FBH] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_FBL] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_CBIT] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_ADDC] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_SUBB] = {
      .gen = GEN_GE(GEN7),
   },
   [BRW_OPCODE_SAD2] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_SADA2] = {
      .gen = GEN_ALL,
   },
   /* Reserved 82-83 */
   [BRW_OPCODE_DP4] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_DPH] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_DP3] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_DP2] = {
      .gen = GEN_ALL,
   },
   /* Reserved 88 */
   [BRW_OPCODE_LINE] = {
      .gen = GEN_ALL,
   },
   [BRW_OPCODE_PLN] = {
      .gen = GEN_GE(GEN45),
   },
   [BRW_OPCODE_MAD] = {
      .gen = GEN_GE(GEN6),
   },
   [BRW_OPCODE_LRP] = {
      .gen = GEN_GE(GEN6),
   },
   /* Reserved 93-124 */
   /* BRW_OPCODE_NENOP */
   [BRW_OPCODE_NOP] = {
      .gen = GEN_ALL,
   },
};

static unsigned
num_sources_from_inst(const struct brw_device_info *devinfo,
                      const brw_inst *inst)
{
   unsigned math_function;

   if (brw_inst_opcode(devinfo, inst) == BRW_OPCODE_MATH) {
      math_function = brw_inst_math_function(devinfo, inst);
   } else if (devinfo->gen < 6 &&
              brw_inst_opcode(devinfo, inst) == BRW_OPCODE_SEND) {
      if (brw_inst_sfid(devinfo, inst) == BRW_SFID_MATH) {
         math_function = brw_inst_math_msg_function(devinfo, inst);
      } else {
         /* Send instructions are allowed to have null sources since they use
          * the base_mrf field to specify which message register source.
          */
         return 0;
      }
   } else {
      return opcode_descs[brw_inst_opcode(devinfo, inst)].nsrc;
   }

   switch (math_function) {
   case BRW_MATH_FUNCTION_INV:
   case BRW_MATH_FUNCTION_LOG:
   case BRW_MATH_FUNCTION_EXP:
   case BRW_MATH_FUNCTION_SQRT:
   case BRW_MATH_FUNCTION_RSQ:
   case BRW_MATH_FUNCTION_SIN:
   case BRW_MATH_FUNCTION_COS:
   case BRW_MATH_FUNCTION_SINCOS:
   case GEN8_MATH_FUNCTION_INVM:
   case GEN8_MATH_FUNCTION_RSQRTM:
      return 1;
   case BRW_MATH_FUNCTION_FDIV:
   case BRW_MATH_FUNCTION_POW:
   case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER:
   case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT:
   case BRW_MATH_FUNCTION_INT_DIV_REMAINDER:
      return 2;
   default:
      unreachable("not reached");
   }
}

static enum gen
gen_from_devinfo(const struct brw_device_info *devinfo)
{
   switch (devinfo->gen) {
   case 4: return devinfo->is_g4x ? GEN45 : GEN4;
   case 5: return GEN5;
   case 6: return GEN6;
   case 7: return devinfo->is_haswell ? GEN75 : GEN7;
   case 8: return GEN8;
   case 9: return GEN9;
   default:
      unreachable("not reached");
   }
}

static bool
is_unsupported_inst(const struct brw_device_info *devinfo,
                    const brw_inst *inst)
{
   enum gen gen = gen_from_devinfo(devinfo);
   return (inst_info[brw_inst_opcode(devinfo, inst)].gen & gen) == 0;
}

bool
brw_validate_instructions(const struct brw_codegen *p, int start_offset,
                          struct annotation_info *annotation)
{
   const struct brw_device_info *devinfo = p->devinfo;
   const void *store = p->store + start_offset / 16;
   bool valid = true;

   for (int src_offset = 0; src_offset < p->next_insn_offset - start_offset;
        src_offset += sizeof(brw_inst)) {
      struct string error_msg = { .str = NULL, .len = 0 };
      const brw_inst *inst = store + src_offset;

      switch (num_sources_from_inst(devinfo, inst)) {
      case 3:
         /* Nothing to test. 3-src instructions can only have GRF sources, and
          * there's no bit to control the file.
          */
         break;
      case 2:
         ERROR_IF(src1_is_null(devinfo, inst), "src1 is null");
         /* fallthrough */
      case 1:
         ERROR_IF(src0_is_null(devinfo, inst), "src0 is null");
         break;
      case 0:
      default:
         break;
      }

      ERROR_IF(is_unsupported_inst(devinfo, inst),
               "Instruction not supported on this Gen");

      if (error_msg.str && annotation) {
         annotation_insert_error(annotation, src_offset, error_msg.str);
      }
      free(error_msg.str);
   }

   return valid;
}
