/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "util/lut.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

static enum jay_type
canonicalize_for_bit_compare(enum jay_type type)
{
   enum jay_type base = jay_base_type(type);
   return (base == JAY_TYPE_S) ? jay_type_rebase(type, JAY_TYPE_U) : type;
}

static bool
propagate_cmod(jay_function *func, jay_inst *I, jay_inst **defs)
{
   enum jay_type cmp_type = I->type;
   enum jay_conditional_mod cmod = I->conditional_mod;
   jay_inst *def = NULL;

   /* TODO: Generalize cmod propagation */
   if (jay_type_size_bits(cmp_type) != 32)
      return false;

   /* Pattern match `cmp ssa, 0` or `cmp 0, ssa`. */
   jay_foreach_ssa_src(I, s) {
      if (jay_is_zero(I->src[1 - s])) {
         def = defs[jay_base_index(I->src[s])];

         /* Canonicalize the cmod to have the zero second */
         cmod = s == 1 ? jay_conditional_mod_swap_sources(cmod) : cmod;
         break;
      }
   }

   /* Check if we can fold into the def */
   if (!def || !jay_is_null(def->cond_flag) || !jay_opcode_infos[def->op].cmod)
      return false;

   /* bfn bspec says "only zero(ze), greater-than(gt), and less-than(lt)
    * conditional modifiers are valid."
    */
   if (def->op == JAY_OPCODE_BFN && !(cmod == JAY_CONDITIONAL_EQ ||
                                      cmod == JAY_CONDITIONAL_GT ||
                                      cmod == JAY_CONDITIONAL_LT)) {
      return false;
   }

   /* "Neither Saturate nor conditional modifier allowed with DW integer
    * multiply."
    *
    * Could be refined.
    */
   if (def->op == JAY_OPCODE_MUL && !jay_type_is_any_float(def->type))
      return false;

   enum jay_type instr_type = def->type;

   if (cmod == JAY_CONDITIONAL_NE || cmod == JAY_CONDITIONAL_EQ) {
      cmp_type = canonicalize_for_bit_compare(cmp_type);
      instr_type = canonicalize_for_bit_compare(instr_type);
   }

   if (instr_type != cmp_type)
      return false;

   jay_builder b = jay_init_builder(func, jay_before_inst(I));
   jay_set_conditional_mod(&b, def, I->cond_flag, cmod);
   return true;
}

static jay_def
jay_compose_src(jay_def to, jay_def from)
{
   if (to.abs) {
      from.negate = false;
      from.abs = true;
   }

   from.negate ^= to.negate;
   return from;
}

static void
propagate_modifier(jay_inst *I, unsigned s, jay_inst *mod)
{
   /* Check if we can propagate abs/neg here in general */
   if (!jay_has_src_mods(I, s) ||
       mod->saturate ||
       jay_src_type(I, s) != mod->type)
      return;

   jay_replace_src(&I->src[s], mod->src[0]);
   I->src[s] = jay_compose_src(I->src[s], mod->src[0]);
}

static void
propagate_not(jay_inst *I, unsigned s, jay_inst *mod)
{
   /* Handle inot specially for predicates, and logic operations per bspec text:
    *
    *    When used with logic instructions (and, not, or, xor), [the
    *    negate] field indicates whether the source bits are
    *    inverted... regardless of the source type.
    */
   if ((s == I->num_srcs - I->predication) ||
       I->op == JAY_OPCODE_AND ||
       I->op == JAY_OPCODE_OR ||
       I->op == JAY_OPCODE_XOR) {
      jay_replace_src(&I->src[s], mod->src[0]);
      I->src[s].negate ^= true;
   } else if (I->op == JAY_OPCODE_BFN) {
      jay_replace_src(&I->src[s], mod->src[0]);
      jay_set_bfn_ctrl(I, util_lut3_invert_source(jay_bfn_ctrl(I), s));
   }
}

static void
propagate_forwards(jay_function *f)
{
   jay_inst **defs = calloc(f->ssa_alloc, sizeof(defs[0]));
   uint32_t *def_block = malloc(f->ssa_alloc * sizeof(def_block[0]));

   jay_foreach_inst_in_func_safe(f, block, I) {
      jay_builder b = jay_init_builder(f, jay_before_inst(I));

      jay_foreach_dst_index(I, _, d) {
         defs[d] = I;
         def_block[d] = block->index;
      }

      /* Copy propagate individual components into vectors */
      jay_foreach_src_index(I, s, c, idx) {
         jay_inst *def = defs[idx];
         assert(def != NULL && "SSA");

         if (def->op == JAY_OPCODE_MOV &&
             !def->predication &&
             jay_num_values(def->dst) == 1 &&
             jay_num_values(def->src[0]) == 1 &&
             I->src[s].file == def->src[0].file) {

            jay_insert_channel(&b, &I->src[s], c, def->src[0]);
         }
      }

      /* Don't propagate into phis yet - TODO: File awareness */
      if (I->op == JAY_OPCODE_PHI_SRC || I->op == JAY_OPCODE_SEND)
         continue;

      jay_foreach_ssa_src(I, s) {
         /* Copy propagate whole vectors */
         jay_def src = I->src[s];
         if (src.collect)
            continue;

         jay_inst *def = defs[jay_base_index(src)];
         assert(def != NULL && "SSA");

         if (!jay_defs_equivalent(def->dst, src) || def->predication)
            continue;

         if (def->op == JAY_OPCODE_MOV) {
            /* Default values must have the same file as their dest, do not
             * propagate invalid there. Also don't propagate inverse-ballots.
             *
             * For balloted flags, only source 0 can read ARF (i.e. ballotted
             * flags). Furthermore, we may only propagate ballots locally as the
             * ballot is implicitly execmask'd which changes throughout the CFG.
             */
            if ((I->src[s].file == def->src[0].file) ||
                ((!jay_inst_has_default(I) ||
                  &I->src[s] != jay_inst_get_default(I)) &&
                 !(I->src[s].file == UFLAG && !jay_is_imm(def->src[0])) &&
                 !(I->src[s].file == FLAG) &&
                 (!jay_is_flag(def->src[0]) ||
                  (s == 0 && def_block[jay_base_index(src)] == block->index)) &&
                 !(jay_is_imm(def->src[0]) && I->src[s].negate))) {

               jay_replace_src(&I->src[s], def->src[0]);
            }
         } else if (def->op == JAY_OPCODE_MODIFIER && !jay_uses_flag(def)) {
            propagate_modifier(I, s, def);
         } else if (def->op == JAY_OPCODE_NOT && !jay_uses_flag(def)) {
            propagate_not(I, s, def);
         }
      }

      if (I->op == JAY_OPCODE_CMP && propagate_cmod(f, I, defs)) {
         /* Even if we propagate the predicate write, there might be uses of the
          * register value (TODO: Maybe check for this and skip propagating in
          * that case?). So we cannot remove the compare, just strip the cond
          * flag. Furthermore the CMP we always clobber some predicate, so give
          * it an immediately-dead one instead.
          */
         I->cond_flag = jay_alloc_def(&b, I->cond_flag.file, 1);
         continue;
      }
   }

   free(defs);
   free(def_block);
}

static bool
propagate_fsat(jay_inst *I, jay_inst *fsat)
{
   if (fsat->op != JAY_OPCODE_MODIFIER ||
       fsat->predication ||
       fsat->src[0].negate ||
       fsat->src[0].abs ||
       (fsat->conditional_mod && !jay_opcode_infos[I->op].cmod) ||
       I->conditional_mod ||
       I->type != fsat->type ||
       !jay_type_is_any_float(fsat->type))
      return false;

   /* saturate(saturate(x)) = saturate(x) */
   I->saturate |= fsat->saturate;
   I->dst = fsat->dst;
   I->cond_flag = fsat->cond_flag;
   I->conditional_mod = fsat->conditional_mod;
   return true;
}

/*
 * Locally fuse flag AND/OR by converting to predication with tied sources.
 * While easy in SSA, this relies on RA coalescing everything for profitability.
 *
 *    f0 = cmp a, b            f0 = cmp a, b
 *    f1 = cmp c, d    ---->
 *    f2 = and f0, f1          f2 = (f0|f0) cmp c, d
 */
static bool
local_fuse_flag_and_or(jay_function *f,
                       jay_inst *I,
                       jay_inst *use,
                       BITSET_WORD *defined)
{
   /* TODO: Generalize */
   if (I->op != JAY_OPCODE_CMP ||
       jay_type_size_bits(I->type) == 1 ||
       jay_type_size_bits(I->type) > 32 ||
       !(use->op == JAY_OPCODE_AND || use->op == JAY_OPCODE_OR) ||
       use->src[0].negate ||
       use->src[1].negate) {
      return false;
   }

   assert(jay_is_null(I->dst) && !I->predication);
   unsigned i = jay_defs_equivalent(use->src[0], I->cond_flag) ? 0 : 1;
   assert(jay_defs_equivalent(use->src[i], I->cond_flag));
   jay_def other = use->src[1 - i];

   /* We must ensure `other` dominates I. Because defs precede uses and we only
    * work locally, it suffices to check that `other` is defined before I.
    * Counterintuitively, that means we ensure that `other` has NOT yet been
    * defined when processing I - because we propagate backwards.
    *
    * Currently we also bail on mixed FLAG/UFLAG cases for simplicity.
    */
   if (BITSET_TEST(defined, jay_index(other)) ||
       use->src[0].file != use->src[1].file) {
      return false;
   }

   /* Convert to predication using the identities:
    *
    *    a & b = a ? b : 0 = a ? b : a
    *    a | b = a ? 1 : b = a ? a : b
    */
   I->cond_flag = use->dst;
   jay_def pred = use->op == JAY_OPCODE_OR ? jay_negate(other) : other;
   jay_builder b = jay_init_builder(f, jay_before_inst(I));
   jay_add_predicate_else(&b, I, pred, other);
   return true;
}

static void
propagate_backwards(jay_function *f)
{
   jay_inst **uses = calloc(f->ssa_alloc, sizeof(uses[0]));
   BITSET_WORD *multiple = BITSET_CALLOC(f->ssa_alloc);
   BITSET_WORD *defined = BITSET_CALLOC(f->ssa_alloc);
   uint32_t *def_block = malloc(f->ssa_alloc * sizeof(def_block[0]));

   jay_foreach_inst_in_func_safe_rev(f, block, I) {
      jay_foreach_dst_index(I, _, index) {
         BITSET_SET(defined, index);
      }

      /* Record uses */
      jay_foreach_src_index(I, s, c, ssa_index) {
         if (uses[ssa_index])
            BITSET_SET(multiple, ssa_index);
         else
            uses[ssa_index] = I;
      }

      bool flag = jay_is_null(I->dst);
      jay_def dst = flag ? I->cond_flag : I->dst;

      /* TODO: f64 sat propagation */
      if (jay_num_values(dst) != 1)
         continue;

      def_block[jay_base_index(dst)] = block->index;

      assert(jay_is_ssa(dst));
      jay_inst *use = uses[jay_base_index(dst)];
      if (!use || BITSET_TEST(multiple, jay_base_index(dst)))
         continue;

      if (def_block[jay_base_index(use->dst)] == block->index &&
          local_fuse_flag_and_or(f, I, use, defined)) {

         jay_remove_instruction(use);
         continue;
      }

      if (!flag &&
          jay_opcode_infos[I->op].sat &&
          jay_type_is_any_float(I->type) &&
          propagate_fsat(I, use)) {

         jay_remove_instruction(use);
         continue;
      }

      /* Fold UGPR->{GPR, FLAG} and UFLAG->FLAG copies coming out of NIR.
       * Inverse-ballots are propagated only locally.
       */
      if (use->type ==
             (flag ? JAY_TYPE_U1 : canonicalize_for_bit_compare(I->type)) &&
          I->op != JAY_OPCODE_PHI_DST &&
          use->op == JAY_OPCODE_MOV &&
          use->dst.file != J_ADDRESS &&
          (!jay_is_flag(use->dst) ||
           def_block[jay_base_index(use->dst)] == block->index)) {

         *(flag ? &I->cond_flag : &I->dst) = use->dst;
         jay_remove_instruction(use);
         continue;
      }
   }

   free(defined);
   free(def_block);
   free(multiple);
   free(uses);
}

JAY_DEFINE_FUNCTION_PASS(jay_opt_propagate_forwards, propagate_forwards)
JAY_DEFINE_FUNCTION_PASS(jay_opt_propagate_backwards, propagate_backwards)
