/*
 * Copyright (C) 2022 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include "agx_compiler.h"
#include "agx_builder.h"

/* Lower pseudo instructions created during optimization. */
static agx_instr *
lower(agx_builder *b, agx_instr *I)
{
   switch (I->op) {

   /* Various instructions are implemented as bitwise truth tables */
   case AGX_OPCODE_MOV:
      return agx_bitop_to(b, I->dest[0], I->src[0], agx_zero(), AGX_BITOP_MOV);

   case AGX_OPCODE_NOT:
      return agx_bitop_to(b, I->dest[0], I->src[0], agx_zero(), AGX_BITOP_NOT);

   case AGX_OPCODE_AND:
      return agx_bitop_to(b, I->dest[0], I->src[0], I->src[1], AGX_BITOP_AND);

   case AGX_OPCODE_XOR:
      return agx_bitop_to(b, I->dest[0], I->src[0], I->src[1], AGX_BITOP_XOR);

   case AGX_OPCODE_OR:
      return agx_bitop_to(b, I->dest[0], I->src[0], I->src[1], AGX_BITOP_OR);

   default: return NULL;
   }
}

void
agx_lower_pseudo(agx_context *ctx)
{
   agx_foreach_instr_global_safe(ctx, I) {
      agx_builder b = agx_init_builder(ctx, agx_before_instr(I));

      if (lower(&b, I))
         agx_remove_instruction(I);
   }
}
