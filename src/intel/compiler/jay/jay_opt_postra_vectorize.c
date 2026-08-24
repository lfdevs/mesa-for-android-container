/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"

static void
vectorize(jay_function *f, jay_inst **vec, unsigned n)
{
   if (n > 1) {
      jay_builder b = jay_init_builder(f, jay_before_inst(vec[0]));

      unsigned i = 0;
      while (i < n) {
         unsigned simd = 1 << util_logbase2(n - i);
         jay_inst *clone = jay_clone_inst(&b, vec[0], vec[0]->num_srcs);
         clone->dst = jay_bare_regs(UGPR, vec[0]->dst.reg + i, simd);
         clone->src[0] = jay_bare_regs(UGPR, vec[0]->src[0].reg + i, simd);
         jay_builder_insert(&b, clone);
         i += simd;
      }

      for (unsigned i = 0; i < n; ++i) {
         jay_remove_instruction(vec[i]);
      }
   }
}

static bool
compatible_regs(jay_def xI, jay_def x0, unsigned n, unsigned ugpr_per_grf_log2)
{
   return xI.reg == x0.reg + n &&
          (xI.reg >> ugpr_per_grf_log2) == (x0.reg >> ugpr_per_grf_log2);
}

static bool
reg_in_range(jay_def reg, jay_def base, unsigned n)
{
   return reg.reg >= base.reg && reg.reg < base.reg + n;
}

static bool
is_compatible(jay_shader *s, jay_inst **vec, unsigned n, const jay_inst *I)
{
   /* If we're not vectorizable, don't vectorize with anything */
   if (!((I->op == JAY_OPCODE_MOV && I->type == JAY_TYPE_U32) &&
         (I->dst.file == UGPR && I->src[0].file == UGPR))) {
      return false;
   }

   /* A single instruction is vacuously compatible with itself */
   if (n == 0) {
      return true;
   }

   /* If we would overflow the SIMD width limit, stop vectorizing */
   if (n >= (s->devinfo->ver >= 20 ? 32 : 16)) {
      return false;
   }

   /* We must access consecutive channels from the same GRF without overlap on
    * the source/destination regions.
    */
   unsigned ugpr_per_grf_log2 = util_logbase2(jay_ugpr_per_grf(s));
   return compatible_regs(I->dst, vec[0]->dst, n, ugpr_per_grf_log2) &&
          compatible_regs(I->src[0], vec[0]->src[0], n, ugpr_per_grf_log2) &&
          !reg_in_range(I->dst, vec[0]->dst, n) &&
          !reg_in_range(I->src[0], vec[0]->dst, n) &&
          !reg_in_range(I->dst, vec[0]->src[0], n) &&
          !reg_in_range(I->src[0], vec[0]->src[0], n);
}

static void
pass(jay_function *f)
{
   jay_foreach_block(f, block) {
      jay_inst *vec[32] = { NULL };
      unsigned n = 0;

      jay_foreach_inst_in_block_safe(block, I) {
         if (!is_compatible(f->shader, vec, n, I)) {
            vectorize(f, vec, n);
            n = 0;
         }

         if (is_compatible(f->shader, vec, n, I)) {
            vec[n++] = I;
         }
      }
   }
}

JAY_DEFINE_FUNCTION_PASS(jay_opt_postra_vectorize, pass)
