/*
 * Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "ir3_ra.h"
#include "ir3_shader.h"

struct half_val {
   unsigned flags;
   union {
      uint32_t imm;
      physreg_t reg;
      unsigned const_num;
   };
};

struct eval_val {
   struct half_val halves[2];
   bool undef;
};

struct eval_state {
   struct half_val full[RA_FULL_SIZE];
   struct half_val half[RA_HALF_SIZE];
   struct half_val shared[RA_SHARED_SIZE];
};

struct validate_ctx {
   bool mergedregs;
   struct ir3_instruction *copy_instr;
};

static void
validate_error(struct validate_ctx *ctx, const char *condstr, const char *file,
               unsigned line)
{
   fprintf(stderr, "ir3_lower_copies validation fail at %s:%u: %s\n", file,
           line, condstr);
   fprintf(stderr, "  -> for instruction: ");
   ir3_print_instr(ctx->copy_instr);
   abort();
}

#define validate_assert(ctx, cond)                                             \
   do {                                                                        \
      if (!(cond)) {                                                           \
         validate_error(ctx, #cond, __FILE__, __LINE__);                       \
      }                                                                        \
   } while (0)

static void
regs_init(struct half_val *regs, unsigned n)
{
   /* Initially, all registers symbolically contain themselves. */
   for (unsigned i = 0; i < n; i++) {
      regs[i] = (struct half_val){
         .flags = 0,
         .reg = i,
      };
   }
}

static void
eval_state_init(struct eval_state *state)
{
   regs_init(state->full, ARRAY_SIZE(state->full));
   regs_init(state->half, ARRAY_SIZE(state->half));
   regs_init(state->shared, ARRAY_SIZE(state->shared));
}

static struct half_val *
eval_state_get_regs(struct validate_ctx *ctx, struct eval_state *state,
                    struct ir3_register *reg)
{
   if (reg->flags & IR3_REG_SHARED) {
      return state->shared;
   } else if (!ctx->mergedregs && (reg->flags & IR3_REG_HALF)) {
      return state->half;
   } else {
      return state->full;
   }
}

static struct eval_val
eval_state_get_with_offset(struct validate_ctx *ctx, struct eval_state *state,
                           struct ir3_register *reg, unsigned offset)
{
   struct eval_val val = {};

   if (reg->flags & IR3_REG_IMMED) {
      assert(offset == 0);

      for (unsigned i = 0; i < reg_elem_size(reg); i++) {
         val.halves[i] = (struct half_val){
            .flags = IR3_REG_IMMED,
            .imm = (reg->uim_val >> (16 * i)) & 0xffff,
         };
      }
   } else if (reg->flags & IR3_REG_CONST) {
      assert(offset == 0);

      for (unsigned i = 0; i < reg_elem_size(reg); i++) {
         val.halves[i] = (struct half_val){
            .flags = IR3_REG_CONST,

            /* Const register numbers are in units of full regs. Transform to
             * half regs to be able to distinguish the lower/upper parts.
             */
            .const_num = reg->num * 2 + i,
         };
      }
   } else if (reg->num != INVALID_REG) {
      struct half_val *regs = eval_state_get_regs(ctx, state, reg);

      for (unsigned i = 0; i < reg_elem_size(reg); i++) {
         val.halves[i] = regs[ra_reg_get_physreg(reg) + offset + i];
      }
   } else {
      val.undef = true;
   }

   return val;
}

static struct eval_val
eval_state_get(struct validate_ctx *ctx, struct eval_state *state,
               struct ir3_register *reg)
{
   return eval_state_get_with_offset(ctx, state, reg, 0);
}

static void
eval_state_set_with_offset(struct validate_ctx *ctx, struct eval_state *state,
                           struct ir3_register *reg, unsigned offset,
                           struct eval_val val)
{
   /* Copies from INVALID_REG are ignored. */
   if (val.undef) {
      return;
   }

   struct half_val *regs = eval_state_get_regs(ctx, state, reg);

   for (unsigned i = 0; i < reg_elem_size(reg); i++) {
      regs[ra_reg_get_physreg(reg) + offset + i] = val.halves[i];
   }
}

static void
eval_state_set(struct validate_ctx *ctx, struct eval_state *state,
               struct ir3_register *reg, struct eval_val val)
{
   eval_state_set_with_offset(ctx, state, reg, 0, val);
}

static void
validate_cat1(struct validate_ctx *ctx, struct ir3_instruction *instr)
{
   assert(opc_cat(instr->opc) == 1);

   validate_assert(ctx, instr->cat1.dst_type == TYPE_U16 ||
                           instr->cat1.dst_type == TYPE_U32);
   validate_assert(ctx, instr->cat1.src_type == TYPE_U16 ||
                           instr->cat1.src_type == TYPE_U32);
}

#define eval_instrs(...) MESA_CALL_WITH_LTR_ARGS(_eval_instrs, __VA_ARGS__)
static struct eval_state
_eval_instrs(struct validate_ctx *ctx, struct ir3_instruction *copy_instr,
            struct ir3_instruction *before)
{
   struct eval_state state;
   eval_state_init(&state);

   struct ir3_instruction *first =
      before ? ir3_instr_next(before)
             : ir3_block_get_first_instr(copy_instr->block);
   assert(first);

   /* We can't actually symbolically execute xor instructions. They are only
    * used for swapping registers so just detect such sequences and symbolically
    * swap registers.
    */
   struct ir3_instruction *xor_seq[3];
   unsigned n_xor = 0;

   foreach_instr_from (instr, first, &copy_instr->block->instr_list) {
      if (instr == copy_instr) {
         break;
      }

      if (instr->opc == OPC_MOV) {
         validate_assert(ctx, n_xor == 0);
         validate_cat1(ctx, instr);

         /* cov.u32u16 is allowed, cov.u16u32 not. */
         validate_assert(ctx, instr->cat1.src_type != TYPE_U16 ||
                                 instr->cat1.dst_type == TYPE_U16);

         struct ir3_register *dst = instr->dsts[0];
         struct ir3_register *src = instr->srcs[0];

         /* Note: this works both for mov and cov.u32u16 since halves[1] will
          * implicitly be dropped for the latter.
          */
         eval_state_set(ctx, &state, dst, eval_state_get(ctx, &state, src));
      } else if (instr->opc == OPC_SWZ) {
         validate_assert(ctx, n_xor == 0);
         validate_cat1(ctx, instr);
         validate_assert(ctx, instr->cat1.dst_type == instr->cat1.src_type);

         struct ir3_register *dst0 = instr->dsts[0];
         struct ir3_register *dst1 = instr->dsts[1];
         struct ir3_register *src0 = instr->srcs[0];
         struct ir3_register *src1 = instr->srcs[1];

         struct eval_val dst0_init = eval_state_get(ctx, &state, dst0);
         struct eval_val dst1_init = eval_state_get(ctx, &state, dst1);
         eval_state_set(ctx, &state, dst0, eval_state_get(ctx, &state, src0));
         eval_state_set(ctx, &state, dst1, eval_state_get(ctx, &state, src1));
         eval_state_set(ctx, &state, src0, dst0_init);
         eval_state_set(ctx, &state, src1, dst1_init);
      } else if (instr->opc == OPC_SHR_B) {
         validate_assert(ctx, n_xor == 0);

         /* Only shr.b hdst, src, 16 allowed. */
         validate_assert(ctx, (instr->srcs[1]->flags & IR3_REG_IMMED) &&
                                 instr->srcs[1]->uim_val == 16);

         struct ir3_register *dst = instr->dsts[0];
         struct ir3_register *src = instr->srcs[0];
         validate_assert(ctx, dst->flags & IR3_REG_HALF);
         validate_assert(ctx, !(src->flags & IR3_REG_HALF));

         struct eval_val val = eval_state_get(ctx, &state, src);
         val.halves[0] = val.halves[1];
         eval_state_set(ctx, &state, dst, val);
      } else if (instr->opc == OPC_XOR_B) {
         assert(n_xor < 3);

         validate_assert(ctx, instr->dsts[0]->flags == instr->srcs[0]->flags);
         validate_assert(ctx, instr->dsts[0]->flags == instr->srcs[1]->flags);

         xor_seq[n_xor++] = instr;

         if (n_xor == 3) {
            /* swap a, b:
             * xor a, a, b
             * xor b, b, a
             * xor a, a, b
             */
            struct ir3_instruction *xor0 = xor_seq[0];
            struct ir3_instruction *xor1 = xor_seq[1];
            struct ir3_instruction *xor2 = xor_seq[2];
            struct ir3_register *dst = xor0->dsts[0];
            struct ir3_register *src = xor0->srcs[1];
            unsigned dst_num = ra_reg_get_physreg(dst);
            unsigned src_num = ra_reg_get_physreg(src);

            validate_assert(ctx, ra_reg_get_physreg(xor0->srcs[0]) == dst_num);
            validate_assert(ctx, ra_reg_get_physreg(xor1->dsts[0]) == src_num);
            validate_assert(ctx, ra_reg_get_physreg(xor1->srcs[0]) == src_num);
            validate_assert(ctx, ra_reg_get_physreg(xor1->srcs[1]) == dst_num);
            validate_assert(ctx, ra_reg_get_physreg(xor2->dsts[0]) == dst_num);
            validate_assert(ctx, ra_reg_get_physreg(xor2->srcs[0]) == dst_num);
            validate_assert(ctx, ra_reg_get_physreg(xor2->srcs[1]) == src_num);

            struct eval_val dst_init = eval_state_get(ctx, &state, dst);
            eval_state_set(ctx, &state, dst, eval_state_get(ctx, &state, src));
            eval_state_set(ctx, &state, src, dst_init);

            n_xor = 0;
         }
      } else {
         UNREACHABLE("unexpected opcode");
      }
   }

   return state;
}

static struct eval_state
eval_copy(struct validate_ctx *ctx, struct ir3_instruction *instr)
{
   /* We copy from `before` to `after` to simulate parallel copies. */
   struct eval_state before, after;
   eval_state_init(&before);
   eval_state_init(&after);

   if (instr->opc == OPC_META_PARALLEL_COPY) {
      for (unsigned i = 0; i < instr->dsts_count; i++) {
         struct ir3_register *dst = instr->dsts[i];
         struct ir3_register *src = instr->srcs[i];

         for (unsigned j = 0; j < reg_elems(src); j++) {
            unsigned offset = j * reg_elem_size(src);
            struct eval_val val =
               eval_state_get_with_offset(ctx, &before, src, offset);
            eval_state_set_with_offset(ctx, &after, dst, offset, val);
         }
      }
   } else if (instr->opc == OPC_META_COLLECT) {
      struct ir3_register *dst = instr->dsts[0];

      for (unsigned i = 0; i < instr->srcs_count; i++) {
         struct ir3_register *src = instr->srcs[i];
         struct eval_val val = eval_state_get(ctx, &before, src);
         eval_state_set_with_offset(ctx, &after, dst, reg_elem_size(dst) * i,
                                    val);
      }
   } else if (instr->opc == OPC_META_SPLIT) {
      struct ir3_register *dst = instr->dsts[0];
      struct ir3_register *src = instr->srcs[0];
      struct eval_val val = eval_state_get_with_offset(
         ctx, &before, src, instr->split.off * reg_elem_size(src));
      eval_state_set(ctx, &after, dst, val);
   } else {
      UNREACHABLE("unexpected opcode");
   }

   return after;
}

static void
validate_reg(struct validate_ctx *ctx, struct half_val *actual,
             struct half_val *expected)
{
   validate_assert(ctx, actual->flags == expected->flags);

   if (actual->flags & IR3_REG_IMMED) {
      validate_assert(ctx, actual->imm == expected->imm);
   } else if (actual->flags & IR3_REG_CONST) {
      validate_assert(ctx, actual->const_num == expected->const_num);
   } else {
      validate_assert(ctx, actual->reg == expected->reg);
   }
}

static void
validate_regs(struct validate_ctx *ctx, struct half_val *actual,
              struct half_val *expected, unsigned n)
{
   for (unsigned i = 0; i < n; i++) {
      validate_reg(ctx, &actual[i], &expected[i]);
   }
}

/* Validation for ir3_lower_copies. It should be called after a copy instruction
 * (pcopy, collect, split) has been lowered to a sequence of real instructions
 * but before it itself is removed. This will validate that the effects of the
 * instructions in the (before, copy_instr) range are equivalent to copy_instr.
 *
 * This is implemented by symbolically executing both the generated instructions
 * as well as the original copy and verifying they result in the same register
 * state. Register state is represented by half registers containing either a
 * register number or a const/imm value. We execute instructions by moving
 * such values around in the register state.
 */
void
_ir3_lower_copies_validate(struct ir3_shader_variant *v,
                          struct ir3_instruction *copy_instr,
                          struct ir3_instruction *before)
{
#ifdef NDEBUG
   return;
#endif

   struct validate_ctx ctx = {
      .copy_instr = copy_instr,
      .mergedregs = v->mergedregs,
   };

   struct eval_state actual = eval_instrs(&ctx, copy_instr, before);
   struct eval_state expected = eval_copy(&ctx, copy_instr);

   validate_regs(&ctx, actual.full, expected.full, ARRAY_SIZE(actual.full));
   validate_regs(&ctx, actual.shared, expected.shared,
                 ARRAY_SIZE(actual.shared));
}
