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
src0_is_null(const struct gen_device_info *devinfo, const brw_inst *inst)
{
   return brw_inst_src0_reg_file(devinfo, inst) == BRW_ARCHITECTURE_REGISTER_FILE &&
          brw_inst_src0_da_reg_nr(devinfo, inst) == BRW_ARF_NULL;
}

static bool
src1_is_null(const struct gen_device_info *devinfo, const brw_inst *inst)
{
   return brw_inst_src1_reg_file(devinfo, inst) == BRW_ARCHITECTURE_REGISTER_FILE &&
          brw_inst_src1_da_reg_nr(devinfo, inst) == BRW_ARF_NULL;
}

static bool
src0_is_grf(const struct gen_device_info *devinfo, const brw_inst *inst)
{
   return brw_inst_src0_reg_file(devinfo, inst) == BRW_GENERAL_REGISTER_FILE;
}

static unsigned
num_sources_from_inst(const struct gen_device_info *devinfo,
                      const brw_inst *inst)
{
   const struct opcode_desc *desc =
      brw_opcode_desc(devinfo, brw_inst_opcode(devinfo, inst));
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
   } else if (desc) {
      return desc->nsrc;
   } else {
      return 0;
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

static bool
is_unsupported_inst(const struct gen_device_info *devinfo,
                    const brw_inst *inst)
{
   return brw_opcode_desc(devinfo, brw_inst_opcode(devinfo, inst)) == NULL;
}

bool
brw_validate_instructions(const struct brw_codegen *p, int start_offset,
                          struct annotation_info *annotation)
{
   const struct gen_device_info *devinfo = p->devinfo;
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

      if (brw_inst_opcode(devinfo, inst) == BRW_OPCODE_SEND) {
         ERROR_IF(brw_inst_src0_address_mode(devinfo, inst) !=
                  BRW_ADDRESS_DIRECT, "send must use direct addressing");

         if (devinfo->gen >= 7) {
            ERROR_IF(!src0_is_grf(devinfo, inst), "send from non-GRF");
            ERROR_IF(brw_inst_eot(devinfo, inst) &&
                     brw_inst_src0_da_reg_nr(devinfo, inst) < 112,
                     "send with EOT must use g112-g127");
         }
      }

      if (error_msg.str && annotation) {
         annotation_insert_error(annotation, src_offset, error_msg.str);
      }
      free(error_msg.str);
   }

   return valid;
}
