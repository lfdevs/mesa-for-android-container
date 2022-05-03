/*
 * Copyright (C) 2021 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "va_compiler.h"
#include "valhall.h"
#include "bi_builder.h"

void
va_lower_isel(bi_instr *I)
{
   switch (I->op) {

   /* Integer addition has swizzles and addition with 0 is canonical swizzle */
   case BI_OPCODE_SWZ_V2I16:
      I->op = BI_OPCODE_IADD_V2U16;
      I->src[1] = bi_zero();
      break;

   case BI_OPCODE_SWZ_V4I8:
      I->op = BI_OPCODE_IADD_V4U8;
      I->src[1] = bi_zero();
      break;

   /* Needs to output the coverage mask */
   case BI_OPCODE_DISCARD_F32:
      assert(bi_is_null(I->dest[0]));
      I->dest[0] = bi_register(60);
      break;

   /* Extra source in Valhall not yet modeled in the Bifrost IR */
   case BI_OPCODE_ICMP_I32:
      I->op = BI_OPCODE_ICMP_U32;
      I->src[2] = bi_zero();
      break;

   case BI_OPCODE_ICMP_V2I16:
      I->op = BI_OPCODE_ICMP_V2U16;
      I->src[2] = bi_zero();
      break;

   case BI_OPCODE_ICMP_V4I8:
      I->op = BI_OPCODE_ICMP_V4U8;
      I->src[2] = bi_zero();
      break;

   case BI_OPCODE_ICMP_U32:
   case BI_OPCODE_ICMP_V2U16:
   case BI_OPCODE_ICMP_V4U8:
   case BI_OPCODE_ICMP_S32:
   case BI_OPCODE_ICMP_V2S16:
   case BI_OPCODE_ICMP_V4S8:
   case BI_OPCODE_FCMP_F32:
   case BI_OPCODE_FCMP_V2F16:
      I->src[2] = bi_zero();
      break;

   /* Integer CSEL must have a signedness */
   case BI_OPCODE_CSEL_I32:
   case BI_OPCODE_CSEL_V2I16:
      assert(I->cmpf == BI_CMPF_EQ || I->cmpf == BI_CMPF_NE);

      I->op = (I->op == BI_OPCODE_CSEL_I32) ? BI_OPCODE_CSEL_U32 :
              BI_OPCODE_CSEL_V2U16;
      break;

   /* Jump -> conditional branch with condition tied to true. */
   case BI_OPCODE_JUMP:
      I->op = I->branch_target ? BI_OPCODE_BRANCHZ_I16 : BI_OPCODE_BRANCHZI;
      I->src[1] = I->src[0];
      I->src[0] = bi_zero();
      I->cmpf = BI_CMPF_EQ;
      break;

   case BI_OPCODE_AXCHG_I32:
      I->op = BI_OPCODE_ATOM_RETURN_I32;
      I->atom_opc = BI_ATOM_OPC_AXCHG;
      I->sr_count = 1;
      break;

   case BI_OPCODE_ACMPXCHG_I32:
      I->op = BI_OPCODE_ATOM_RETURN_I32;
      I->atom_opc = BI_ATOM_OPC_ACMPXCHG;
      /* Reads 2, this is special cased in bir.c */
      I->sr_count = 1;
      break;

   case BI_OPCODE_ATOM_RETURN_I32:
      if (bi_is_null(I->dest[0]))
         I->op = BI_OPCODE_ATOM_I32;

      break;

   default:
      break;
   }
}
