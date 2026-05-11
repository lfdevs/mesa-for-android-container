/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * Instruction selection works on SSA FLAG and UFLAG variables. This pass
 * implements a flag register allocator, assigning each FLAG/UFLAG either to a
 * hardware flag register and/or spilling to a GPR/UGPR.
 *
 * As a simplification, hardware flags are block-local. At block boundaries,
 * 32-bit 0/~0 (U)GPRs are our canonical representation for (U)FLAGs.
 *
 * Producers: CMP produce both 0/~0 GPRs and flags, while conditional modifiers
 * produce only flags. Boolean arithmetic is lowered to GPRs.
 *
 * Consumers: SEL/CSEL consumes both GPRs and flags, while predication consumes
 * only flags. Boolean arithmetic again requires GPRs.
 *
 * Our strategy is to turn flags into GPR representations globally while keeping
 * copies in flags where it makes sense locally.
 */

static inline jay_def
canonicalize_flag(jay_def x)
{
   assert(jay_is_flag(x));
   x.file = x.file == UFLAG ? UGPR : GPR;
   return x;
}

struct var_info {
   unsigned flag           :3;
   bool uflag              :1;
   bool simd               :1;
   bool read_by_predication:1;
   bool free_canonical     :1;
   unsigned pad            :1;
} PACKED;
static_assert(sizeof(struct var_info) == 1);

struct flag_ra {
   jay_builder *b;
   BITSET_WORD *ballot_blocks;
   jay_block *block;
   struct var_info *vars;
   unsigned nr_vars;
   uint32_t flag_to_global[JAY_MAX_FLAGS];
   uint32_t flag_to_local[JAY_MAX_FLAGS];
   unsigned roundrobin;
};

static jay_def
assign_flag(struct flag_ra *ra,
            jay_def flag,
            enum jay_file file,
            bool broadcast,
            bool free_canonical,
            bool ballot,
            jay_def *tie)
{
   assert(!ballot || BITSET_TEST(ra->ballot_blocks, ra->block->index));

   jay_def canonical = canonicalize_flag(flag);
   jay_def tmp = jay_alloc_def(ra->b, file, 1);

   unsigned num_flags = jay_num_regs(ra->b->shader, FLAG);
   tmp.reg = tie ? tie->reg : ballot ? 0 : ((ra->roundrobin++) % num_flags);

   /* Uniform access (via a UFLAG or an inverse-ballot) would clobber the zero
    * for a ballot. We could refine this further but this should be ok for now.
    */
   if (!ballot &&
       tmp.reg == 0 &&
       BITSET_TEST(ra->ballot_blocks, ra->block->index)) {

      assert(!tie);
      tmp.reg = 1;
      ra->roundrobin++;
   }

   if (jay_index(canonical) < ra->nr_vars) {
      ra->vars[jay_index(canonical)] = (struct var_info) {
         .uflag = tmp.file == UFLAG,
         .simd = tmp.file == FLAG || broadcast,
         .flag = tmp.reg,
         .free_canonical = free_canonical,
      };
   }

   ra->flag_to_global[tmp.reg] = jay_index(canonical);
   ra->flag_to_local[tmp.reg] = jay_index(tmp);
   return tmp;
}

static bool
rewrite_sel_with_zero(jay_inst *I, unsigned zero)
{
   jay_def flag = I->src[2];
   unsigned other = 1 - zero;

   if (!jay_defs_equivalent(I->src[zero], jay_imm(0)) ||
       I->src[other].abs ||
       I->src[other].negate ||
       I->saturate ||
       jay_type_size_bits(I->type) != 32) {
      return false;
   }

   /* If no abs/negate/saturate are set, even if we are a SEL.f32 we are not
    * necessarily going to canonicalize denorms so it's safe to use integers.
    */
   I->type = jay_type_rebase(I->type, JAY_TYPE_U);

   if (jay_defs_equivalent(I->src[other], jay_imm(0xffffffff)) && zero == 1) {
      /* (c ? 0xffffffff : 0) -> canonical(c) */
      I->op = JAY_OPCODE_MOV;
      I->src[0] = canonicalize_flag(flag);
      jay_shrink_sources(I, 1);
   } else {
      /* ([!]c ? a : 0) --> (a &  [~]canonical(c)) and
       * ([!]c ? 0 : a) --> (a & ~[~]canonical(c))
       */
      I->op = JAY_OPCODE_AND;
      I->src[0] = I->src[other];
      I->src[1] = canonicalize_flag(flag);
      I->src[1].negate ^= (zero == 0);
      jay_shrink_sources(I, 2);
   }

   return true;
}

static bool
rewrite_sel_to_csel(jay_inst *I)
{
   if (jay_type_size_bits(I->type) != 32) {
      return false;
   }

   /* SEL.f32 lowers to CSEL.f32 to preserve source modifiers & float controls.
    * That works since we reinterpret 0/~0 as 0.0/NaN.
    */
   jay_def flag = I->src[2];
   I->op = JAY_OPCODE_CSEL;
   I->conditional_mod = flag.negate ? JAY_CONDITIONAL_EQ : JAY_CONDITIONAL_NE;
   I->src[2] = canonicalize_flag(flag);
   I->src[2].negate = false;
   return true;
}

static bool
rewrite_without_flag(struct flag_ra *ra, jay_inst *I, unsigned s, bool in_flag)
{
   if (I->op == JAY_OPCODE_PHI_SRC) {
      I->src[s] = canonicalize_flag(I->src[s]);
      return true;
   }

   if (jay_debug & JAY_DBG_NOOPT) {
      return false;
   }

   if (I->op == JAY_OPCODE_SEL &&
       (!in_flag || ra->vars[jay_index(I->src[s])].free_canonical) &&
       !I->predication) {

      return rewrite_sel_with_zero(I, 0) ||
             rewrite_sel_with_zero(I, 1) ||
             (!in_flag && rewrite_sel_to_csel(I));
   }

   return false;
}

static void
assign_block(struct flag_ra *ra)
{
   jay_builder *b = ra->b;

   jay_foreach_inst_in_block_safe(ra->block, I) {
      if (I->op == JAY_OPCODE_CAST_CANONICAL_TO_FLAG) {
         /* Assume the source is already 0/~0 canonical and use it. */
         I->op = JAY_OPCODE_MOV;
         I->type = JAY_TYPE_U32;
         I->dst = canonicalize_flag(I->dst);
         continue;
      } else if (I->type == JAY_TYPE_U1) {
         /* Boolean logic turns into bitwise logic on the canonical form */
         if (!jay_is_null(I->dst)) {
            I->dst = canonicalize_flag(I->dst);
         }

         jay_foreach_src(I, s) {
            if (!(s == 2 && I->op == JAY_OPCODE_SEL) &&
                jay_src_type(I, s) == JAY_TYPE_U1) {
               if (jay_is_imm(I->src[s])) {
                  /* Convert 1-bit boolean to 0/~0 */
                  assert(jay_is_imm(I->src[s]) && jay_as_uint(I->src[s]) <= 1);
                  I->src[s] = jay_imm(jay_as_uint(I->src[s]) ? ~0 : 0);
               } else {
                  I->src[s] = canonicalize_flag(I->src[s]);
               }
            }
         }

         I->type = JAY_TYPE_U32;
      }

      if (I->op == JAY_OPCODE_CMP && I->predication) {
         jay_def *default_ = jay_inst_get_default(I);
         *default_ = canonicalize_flag(*default_);
      }

      /* Handle flag sources */
      jay_foreach_src(I, s) {
         if (!jay_is_flag(I->src[s])) {
            continue;
         }

         unsigned index = jay_index(I->src[s]);
         bool ballot = jay_src_type(I, s) != JAY_TYPE_U1;
         bool uniform = I->dst.file == UGPR ||
                        (jay_is_null(I->dst) && I->cond_flag.file == UFLAG);
         enum jay_file file = uniform && !ballot ? UFLAG : FLAG;

         bool in_flag =
            ra->flag_to_global[ra->vars[index].flag] == index &&
            ((file == UFLAG) ? ra->vars[index].uflag : ra->vars[index].simd);

         /* If we don't actually need the flag, we're done. */
         if (rewrite_without_flag(ra, I, s, in_flag)) {
            continue;
         }

         /* Otherwise, ensure we have the value in a flag. */
         if (!in_flag) {
            jay_def tmp =
               assign_flag(ra, I->src[s], file, false, false, ballot, NULL);

            /* XXX: We need a more systematic approach to modifiers :/ */
            b->cursor = jay_before_inst(I);
            jay_def d = I->src[s];
            d.negate = false;
            jay_CMP(b, JAY_TYPE_U32, JAY_CONDITIONAL_NE, tmp,
                    canonicalize_flag(d), 0);
         }

         /* ...and rewrite to use the flag */
         unsigned reg = ra->vars[index].flag;
         jay_def flag = jay_scalar(ra->vars[index].uflag ? UFLAG : FLAG,
                                   ra->flag_to_local[reg]);
         flag.reg = reg;
         jay_replace_src(&I->src[s], flag);
      }

      /* Handle flag writes */
      b->cursor = jay_after_inst(I);

      /* If the flag is written directly (for an inverse ballot), recover the
       * canonical representation with a SEL.
       */
      if (!jay_is_null(I->dst) && jay_is_flag(I->dst)) {
         jay_def canonical = canonicalize_flag(I->dst);
         I->dst =
            assign_flag(ra, I->dst, I->dst.file, false, false, false, NULL);
         jay_SEL(b, JAY_TYPE_U32, canonical, ~0, 0, I->dst);
      }

      if (!jay_is_null(I->cond_flag)) {
         jay_def *tie = I->predication ? jay_inst_get_predicate(I) : NULL;
         I->broadcast_flag =
            ra->vars[jay_index(I->cond_flag)].read_by_predication &&
            I->cond_flag.file == UFLAG &&
            I->op == JAY_OPCODE_CMP &&
            !tie;

         jay_def canonical = canonicalize_flag(I->cond_flag);
         I->cond_flag =
            assign_flag(ra, I->cond_flag, I->cond_flag.file, I->broadcast_flag,
                        I->op == JAY_OPCODE_CMP, false, tie);

         if (I->op == JAY_OPCODE_CMP) {
            assert(jay_is_null(I->dst));

            if (I->broadcast_flag) {
               /* We need to recover the UGPR from the replicated FLAG. Thanks
                * to our write-masking and broadcasting, the flag is already
                * 0/~0. We simply need to sign-extend.
                */
               jay_i2i32(b, canonical, b->shader->dispatch_width, I->cond_flag);
            } else if (jay_type_size_bits(I->type) != 32) {
               I->dst = jay_alloc_def(b, canonical.file,
                                      jay_type_vector_length(I->type));
               jay_i2i32(b, canonical, jay_type_size_bits(I->type), I->dst);
            } else {
               /* 32-bit CMP returns the canonical form */
               I->dst = canonical;
            }
         } else {
            assert(jay_type_size_bits(I->type) == 32 && "limited cmod prop");

            if (jay_is_null(I->dst)) {
               I->dst = jay_alloc_def(b, canonical.file,
                                      jay_type_vector_length(I->type));
            }

            /* Recover the canonical representation with a CMP. Hopefully,
             * either the CMP or the cmod will be eliminated by a later DCE.
             */
            jay_inst *cmp =
               jay_CMP(b, I->type, I->conditional_mod, canonical, I->dst, 0);
            cmp->cond_flag =
               assign_flag(ra, cmp->cond_flag, cmp->cond_flag.file,
                           false /* broadcast */, true /* free_canonical */,
                           false /* ballot */, NULL);
         }
      }
   }

   /* Ballots require zeroing the ballot flag (f0) */
   b->cursor = jay_before_block(ra->block);
   if (BITSET_TEST(ra->ballot_blocks, ra->block->index)) {
      jay_ZERO_FLAG(b, 0);
   }
}

static void
copyprop(jay_function *f)
{
   jay_inst **defs = calloc(f->ssa_alloc, sizeof(defs[0]));

   jay_foreach_inst_in_func_safe(f, block, I) {
      jay_foreach_dst_index(I, _, d) {
         defs[d] = I;
      }

      if (I->op == JAY_OPCODE_PHI_SRC || I->op == JAY_OPCODE_SEND)
         continue;

      jay_foreach_ssa_src(I, s) {
         jay_def src = I->src[s];
         if (src.collect)
            continue;

         jay_inst *def = defs[jay_base_index(src)];
         if (jay_defs_equivalent(def->dst, src) &&
             !def->predication &&
             def->op == JAY_OPCODE_MOV &&
             (I->src[s].file == def->src[0].file ||
              (I->op == JAY_OPCODE_CMP && jay_is_imm(def->src[0])))) {

            jay_replace_src(&I->src[s], def->src[0]);
         }
      }
   }

   free(defs);
}

void
jay_assign_flags(jay_shader *s)
{
   jay_foreach_function(s, f) {
      uint32_t nr_vars = f->ssa_alloc;
      struct var_info *map = calloc(nr_vars, sizeof(map[0]));
      uint32_t *def_to_block = calloc(nr_vars, sizeof(def_to_block));
      BITSET_WORD *ballot_blocks = BITSET_CALLOC(f->num_blocks);

      jay_foreach_inst_in_func(f, block, I) {
         if (!jay_is_null(I->cond_flag)) {
            def_to_block[jay_index(I->cond_flag)] = block->index + 1;
         }

         if (I->predication == JAY_PREDICATED) {
            jay_def predicate = *jay_inst_get_predicate(I);
            if (def_to_block[jay_index(predicate)] == block->index + 1) {
               map[jay_index(predicate)].read_by_predication = true;
            }
         }

         jay_foreach_src(I, s) {
            if (jay_is_flag(I->src[s]) &&
                jay_src_type(I, s) != JAY_TYPE_U1 &&
                s < I->num_srcs - I->predication) {

               assert(block->index < f->num_blocks);
               BITSET_SET(ballot_blocks, block->index);
            }
         }
      }

      jay_foreach_block(f, block) {
         jay_builder b = { .shader = f->shader, .func = f };
         struct flag_ra ra = {
            .b = &b,
            .vars = map,
            .nr_vars = nr_vars,
            .ballot_blocks = ballot_blocks,
            .block = block,
         };

         assign_block(&ra);
      }

      free(map);
      free(def_to_block);

      /* Flag RA leaves moves. Clean up after ourselves. */
      copyprop(f);
   }
}
