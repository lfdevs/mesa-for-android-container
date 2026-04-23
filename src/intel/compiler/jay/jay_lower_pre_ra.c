/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitscan.h"
#include "util/hash_table.h"
#include "util/lut.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * Register allocation operates only on power-of-two vectors. Pad out
 * non-power-of-two vectors with null values to simplify RA.
 */
static jay_def
lower_npot_vector(jay_builder *b, jay_def x)
{
   unsigned n = jay_num_values(x);

   if (!util_is_power_of_two_or_zero(n)) {
      uint32_t indices[JAY_MAX_DEF_LENGTH] = { 0 };

      for (unsigned i = 0; i < n; ++i) {
         indices[i] = jay_channel(x, i);
      }

      x = jay_collect(b, x.file, indices, util_next_power_of_two(n));
   }

   assert(util_is_power_of_two_or_zero(jay_num_values(x)) && "post-cond");
   return x;
}

/**
 * Vectors need to be allocated to contiguous registers. Furthermore, we
 * require power-of-two sizes in certain cases, that's handled here too.
 *
 * This means that a value cannot appear in multiple channels of an
 * instruction, as register allocation would need to assign the same value to
 * locations <X+i> and <X+j>. Scalars don't have this restriction, except for
 * SENDs because the hardware bans repeated sources.
 *
 * If a value appears in multiple positions, we emit copies so that each
 * can be register allocated in the correct position.
 */
static void
lower_contiguous_sources(jay_builder *b, jay_inst *I)
{
   b->cursor = jay_before_inst(I);
   uint32_t seen[JAY_MAX_DEF_LENGTH], nr_seen = 0;

   jay_foreach_src(I, s) {
      if (jay_num_values(I->src[s]) > 1 || I->op == JAY_OPCODE_SEND) {
         jay_foreach_index(I->src[s], c, index) {
            /* Search for the index */
            unsigned i;
            for (i = 0; i < nr_seen && seen[i] != index; ++i) {
            }

            if (i == nr_seen) {
               /* Record a new index */
               assert(nr_seen < ARRAY_SIZE(seen));
               seen[nr_seen++] = index;
            } else {
               /* Insert a copy to access a duplicated index */
               jay_def copy = jay_alloc_def(b, I->src[s].file, 1);
               jay_MOV(b, copy, jay_extract(I->src[s], c));
               jay_insert_channel(b, &I->src[s], c, copy);
            }
         }

         jay_replace_src(&I->src[s], lower_npot_vector(b, I->src[s]));
      }
   }
}

static jay_def
lower_imm_to_ugpr(jay_builder *b,
                  jay_inst *I,
                  unsigned s,
                  struct hash_table_u64 *constants)
{
   /* Although only 32-bit constants are supported, 64-bit constants are
    * separate in the key since they must be zero-extended. We could optimize
    * this but it doesn't really matter.
    */
   uint32_t imm = jay_as_uint(I->src[s]);
   bool is_64bit = jay_type_size_bits(jay_src_type(I, s)) == 64;
   uint64_t key = imm | (is_64bit ? BITFIELD64_BIT(32) : 0);

   jay_inst *mov = _mesa_hash_table_u64_search(constants, key);
   if (mov)
      return mov->dst;

   /* Try to use source modifiers to reuse a constant if we can */
   if (jay_src_type(I, s) == JAY_TYPE_F32 && jay_has_src_mods(I, s)) {
      mov = _mesa_hash_table_u64_search(constants, fui(-uif(imm)));
      if (mov)
         return jay_negate(mov->dst);
   }

   /* If this is a new constant, insert a move and cache it. Currently, we pool
    * constants per-function. Inserting everything at the start guarantees that
    * these moves dominate all their uses, although it hurts register pressure.
    * The spiller should rematerialize constants where necessary to ensure we
    * don't lose the wave, but we could still probably optimize this.
    */
   jay_def x = jay_alloc_def(b, UGPR, is_64bit ? 2 : 1);
   b->cursor = jay_before_function(b->func);
   _mesa_hash_table_u64_insert(constants, key, jay_MOV(b, x, imm));
   return x;
}

static bool
try_swap_src01(jay_inst *I)
{
   if (I->op == JAY_OPCODE_SEL) {
      /* sel(a, b, p) = sel(b, a, !p) */
      I->src[2].negate ^= true;
   } else if (I->op == JAY_OPCODE_CMP) {
      I->conditional_mod = jay_conditional_mod_swap_sources(I->conditional_mod);
   } else if (I->op == JAY_OPCODE_BFN) {
      jay_set_bfn_ctrl(I, util_lut3_swap_sources(jay_bfn_ctrl(I), 0, 1));
   } else if (!jay_opcode_infos[I->op]._2src_commutative) {
      /* Nothing to do for commutative, but otherwise we give up */
      return false;
   }

   SWAP(I->src[0], I->src[1]);
   return true;
}

/*
 * Instructions can only encode immediates in certain positions. Lower
 * immediates to moves where necessary.
 */
static void
lower_immediates(jay_builder *b, jay_inst *I, struct hash_table_u64 *constants)
{
   if (I->num_srcs == 0)
      return;

   /* Canonicalize compare-with-zero to increase freedom */
   if (I->op == JAY_OPCODE_CMP &&
       jay_is_zero(I->src[1]) &&
       jay_is_null(I->dst) &&
       I->type == JAY_TYPE_U32 &&
       !I->predication) {

      assert(!jay_is_null(I->cond_flag));
      I->op = JAY_OPCODE_MOV;
      jay_shrink_sources(I, 1);
   }

   /* One source supports immediates but the other does not, so swap. */
   unsigned other = I->op == JAY_OPCODE_BFN ? 1 : 0;
   if (jay_is_imm(I->src[other]) &&
       !_mesa_hash_table_u64_search(constants, jay_as_uint(I->src[other]))) {

      try_swap_src01(I);
   }

   /* Immediates allowed only in certain cases, lower the rest */
   jay_foreach_src(I, s) {
      if (jay_is_imm(I->src[s])) {
         uint32_t imm = jay_as_uint(I->src[s]);

         bool last = s == (jay_num_isa_srcs(I) - 1);
         bool allowed = s < 2 && (last || I->op == JAY_OPCODE_SEND);
         allowed |= (I->op == JAY_OPCODE_BFN && s == 0 && imm < UINT16_MAX);

         if (!allowed) {
            I->src[s] = lower_imm_to_ugpr(b, I, s, constants);
         }
      }
   }
}

void
jay_lower_pre_ra(jay_shader *s)
{
   struct hash_table_u64 *constants = _mesa_hash_table_u64_create(NULL);

   jay_foreach_function(s, f) {
      /* Pool constants per function. */
      _mesa_hash_table_u64_clear(constants);

      jay_foreach_inst_in_func(f, block, I) {
         jay_builder b = { .shader = s, .func = f };

         /* lower_immediates must be last since it consumes I */
         lower_contiguous_sources(&b, I);
         lower_immediates(&b, I, constants);
      }
   }

   _mesa_hash_table_u64_destroy(constants);
}
