/*
 * Copyright 2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_compiler.h"
#include "agx_minifloat.h"

/* AGX peephole optimizer responsible for instruction combining. It operates in
 * a forward direction and a backward direction, in each case traversing in
 * source order. SSA means the forward pass satisfies the invariant:
 *
 *    Every def is visited before any of its uses.
 *
 * Dually, the backend pass satisfies the invariant:
 *
 *    Every use of a def is visited before the def.
 *
 * This means the forward pass can propagate modifiers forward, whereas the
 * backwards pass propagates modifiers backward. Consider an example:
 *
 *    1 = fabs 0
 *    2 = fround 1
 *    3 = fsat 1
 *
 * The forwards pass would propagate the fabs to the fround (since we can
 * lookup the fabs from the fround source and do the replacement). By contrast
 * the backwards pass would propagate the fsat back to the fround (since when
 * we see the fround we know it has only a single user, fsat).  Propagatable
 * instruction have natural directions (like pushforwards and pullbacks).
 *
 * We are careful to update the tracked state whenever we modify an instruction
 * to ensure the passes are linear-time and converge in a single iteration.
 *
 * Size conversions are worth special discussion. Consider the snippet:
 *
 *    2 = fadd 0, 1
 *    3 = f2f16 2
 *    4 = fround 3
 *
 * A priori, we can move the f2f16 in either direction. But it's not equal --
 * if we move it up to the fadd, we get FP16 for two instructions, whereas if
 * we push it into the fround, we effectively get FP32 for two instructions. So
 * f2f16 is backwards. Likewise, consider
 *
 *    2 = fadd 0, 1
 *    3 = f2f32 1
 *    4 = fround 3
 *
 * This time if we move f2f32 up to the fadd, we get FP32 for two, but if we
 * move it down to the fround, we get FP16 to too. So f2f32 is backwards.
 */

static bool
agx_is_fmov(agx_instr *def)
{
   return (def->op == AGX_OPCODE_FADD) &&
          agx_is_equiv(def->src[1], agx_negzero());
}

/* Compose floating-point modifiers with floating-point sources */

static agx_index
agx_compose_float_src(agx_index to, agx_index from)
{
   if (to.abs) {
      from.neg = false;
      from.abs = true;
   }

   from.neg ^= to.neg;

   return from;
}

static void
agx_optimizer_fmov(agx_instr **defs, agx_instr *ins)
{
   agx_foreach_ssa_src(ins, s) {
      agx_index src = ins->src[s];
      agx_instr *def = defs[src.value];

      if (def == NULL)
         continue; /* happens for phis in loops */
      if (!agx_is_fmov(def))
         continue;
      if (def->saturate)
         continue;

      ins->src[s] = agx_compose_float_src(src, def->src[0]);
   }
}

static void
agx_optimizer_inline_imm(agx_instr **defs, agx_instr *I, unsigned srcs,
                         bool is_float)
{
   for (unsigned s = 0; s < srcs; ++s) {
      agx_index src = I->src[s];
      if (src.type != AGX_INDEX_NORMAL)
         continue;
      if (src.neg)
         continue;

      agx_instr *def = defs[src.value];
      if (def->op != AGX_OPCODE_MOV_IMM)
         continue;

      uint8_t value = def->imm;
      uint16_t value_u16 = def->imm;

      bool float_src = is_float;

      /* cmpselsrc takes integer immediates only */
      if (s >= 2 && I->op == AGX_OPCODE_FCMPSEL)
         float_src = false;
      if (I->op == AGX_OPCODE_ST_TILE && s == 0)
         continue;
      if (I->op == AGX_OPCODE_ZS_EMIT && s != 0)
         continue;
      if ((I->op == AGX_OPCODE_DEVICE_STORE ||
           I->op == AGX_OPCODE_LOCAL_STORE || I->op == AGX_OPCODE_ATOMIC ||
           I->op == AGX_OPCODE_LOCAL_ATOMIC) &&
          s != 2)
         continue;
      if ((I->op == AGX_OPCODE_LOCAL_LOAD || I->op == AGX_OPCODE_DEVICE_LOAD) &&
          s != 1)
         continue;

      if (float_src) {
         bool fp16 = (def->dest[0].size == AGX_SIZE_16);
         assert(fp16 || (def->dest[0].size == AGX_SIZE_32));

         float f = fp16 ? _mesa_half_to_float(def->imm) : uif(def->imm);
         if (!agx_minifloat_exact(f))
            continue;

         I->src[s] = agx_immediate_f(f);
      } else if (value == def->imm) {
         I->src[s] = agx_immediate(value);
      } else if (value_u16 == def->imm && agx_allows_16bit_immediate(I)) {
         I->src[s] = agx_abs(agx_immediate(value_u16));
      }
   }
}

static bool
agx_optimizer_fmov_rev(agx_instr *I, agx_instr *use)
{
   if (!agx_is_fmov(use))
      return false;
   if (use->src[0].neg || use->src[0].abs)
      return false;

   /* saturate(saturate(x)) = saturate(x) */
   I->saturate |= use->saturate;
   I->dest[0] = use->dest[0];
   return true;
}

static void
agx_optimizer_copyprop(agx_instr **defs, agx_instr *I)
{
   agx_foreach_ssa_src(I, s) {
      agx_index src = I->src[s];
      agx_instr *def = defs[src.value];

      if (def == NULL)
         continue; /* happens for phis in loops */
      if (def->op != AGX_OPCODE_MOV)
         continue;

      /* At the moment, not all instructions support size conversions. Notably
       * RA pseudo instructions don't handle size conversions. This should be
       * refined in the future.
       */
      if (def->src[0].size != src.size)
         continue;

      /* Immediate inlining happens elsewhere */
      if (def->src[0].type == AGX_INDEX_IMMEDIATE)
         continue;

      /* ALU instructions cannot take 64-bit */
      if (def->src[0].size == AGX_SIZE_64 &&
          !(I->op == AGX_OPCODE_DEVICE_LOAD && s == 0) &&
          !(I->op == AGX_OPCODE_DEVICE_STORE && s == 1) &&
          !(I->op == AGX_OPCODE_ATOMIC && s == 1))
         continue;

      agx_replace_src(I, s, def->src[0]);
   }
}

static void
agx_optimizer_forward(agx_context *ctx)
{
   agx_instr **defs = calloc(ctx->alloc, sizeof(*defs));

   agx_foreach_instr_global(ctx, I) {
      struct agx_opcode_info info = agx_opcodes_info[I->op];

      agx_foreach_ssa_dest(I, d) {
         defs[I->dest[d].value] = I;
      }

      /* Optimize moves */
      agx_optimizer_copyprop(defs, I);

      /* Propagate fmov down */
      if (info.is_float)
         agx_optimizer_fmov(defs, I);

      /* Inline immediates if we can. TODO: systematic */
      if (I->op != AGX_OPCODE_ST_VARY && I->op != AGX_OPCODE_COLLECT &&
          I->op != AGX_OPCODE_TEXTURE_SAMPLE &&
          I->op != AGX_OPCODE_TEXTURE_LOAD &&
          I->op != AGX_OPCODE_UNIFORM_STORE &&
          I->op != AGX_OPCODE_BLOCK_IMAGE_STORE)
         agx_optimizer_inline_imm(defs, I, info.nr_srcs, info.is_float);
   }

   free(defs);
}

static void
agx_optimizer_backward(agx_context *ctx)
{
   agx_instr **uses = calloc(ctx->alloc, sizeof(*uses));
   BITSET_WORD *multiple = calloc(BITSET_WORDS(ctx->alloc), sizeof(*multiple));

   agx_foreach_instr_global_rev(ctx, I) {
      struct agx_opcode_info info = agx_opcodes_info[I->op];

      agx_foreach_ssa_src(I, s) {
         if (I->src[s].type == AGX_INDEX_NORMAL) {
            unsigned v = I->src[s].value;

            if (uses[v])
               BITSET_SET(multiple, v);
            else
               uses[v] = I;
         }
      }

      if (info.nr_dests != 1)
         continue;

      if (I->dest[0].type != AGX_INDEX_NORMAL)
         continue;

      agx_instr *use = uses[I->dest[0].value];

      if (!use || BITSET_TEST(multiple, I->dest[0].value))
         continue;

      /* Destination has a single use, try to propagate */
      if (info.is_float && agx_optimizer_fmov_rev(I, use)) {
         agx_remove_instruction(use);
         continue;
      }
   }

   free(uses);
   free(multiple);
}

void
agx_optimizer(agx_context *ctx)
{
   agx_optimizer_backward(ctx);
   agx_optimizer_forward(ctx);
}
