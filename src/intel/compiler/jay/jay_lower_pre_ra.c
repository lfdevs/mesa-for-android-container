/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/hash_table.h"
#include "util/lut.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/**
 * Vectors need to be allocated to contiguous registers. This means that a value
 * cannot appear in multiple channels of an instruction, as register allocation
 * would need to assign the same value to locations <X+i> and <X+j>. Scalars
 * don't have this restriction, except for SENDs because the hardware bans
 * repeated sources.
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
               jay_def x = jay_extract(I->src[s], c);
               x.negate = x.abs = false; /* XXX: rework modifiers */
               jay_MOV(b, copy, x);
               jay_insert_channel(b, &I->src[s], c, copy);
            }
         }

         jay_replace_src(&I->src[s], I->src[s]);
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
   _mesa_hash_table_u64_insert(constants, key, jay_MOV(b, x, imm));
   return x;
}

static bool
try_swap_src01(jay_inst *I)
{
   if (I->op == JAY_OPCODE_SEL) {
      /* sel(a, b, p) = sel(b, a, !p) */
      I->src[2].negate ^= true;
   } else if (I->op == JAY_OPCODE_CMP || I->op == JAY_OPCODE_DEMOTE) {
      I->conditional_mod = gen_condition_swap_sources(I->conditional_mod);
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
   unsigned nr_srcs = jay_num_isa_srcs(I);
   unsigned other = nr_srcs == 2 ? 0 : 1;
   if ((other < I->num_srcs && jay_is_imm(I->src[other])) &&
       !_mesa_hash_table_u64_search(constants, jay_as_uint(I->src[other]))) {

      try_swap_src01(I);
   }

   /* Immediates allowed only in certain cases, lower the rest */
   jay_foreach_src(I, s) {
      if (jay_is_imm(I->src[s])) {
         /* In general, one machine source cannot take an immediate */
         bool allowed = s < 3 && s != other;

         /* Eligible three-source instructions can have exactly one immediate
          * (src0 or src2) and it must be 16-bit.
          */
         if (nr_srcs == 3) {
            unsigned ver = b->shader->devinfo->ver;
            allowed &= I->op == JAY_OPCODE_BFN ||
                       I->op == JAY_OPCODE_ADD3 ||
                       I->op == JAY_OPCODE_MAD ||
                       I->op == JAY_OPCODE_DP4A_SS ||
                       I->op == JAY_OPCODE_DP4A_SU ||
                       I->op == JAY_OPCODE_DP4A_UU ||
                       (ver >= 12 && I->op == JAY_OPCODE_BFE);

            /* Gen9 does not support 3-src immediates */
            allowed &= ver >= 11 &&
                       !jay_type_is_any_float(I->type) &&
                       jay_as_uint(I->src[s]) <= UINT16_MAX;

            /* Some instructions on some platforms can have at most one
             * immediate source. TODO: Refine.
             */
            allowed &= ((s == 0) || !jay_is_imm(I->src[0]));
         }

         /* SENDs have immediate messages descriptors but not payloads */
         if (I->op == JAY_OPCODE_SEND) {
            allowed = s < 2;
         }

         if (!allowed) {
            I->src[s] = lower_imm_to_ugpr(b, I, s, constants);
         }
      }
   }
}

static void
lower_bf16_restrictions(jay_inst *I, jay_function *f)
{
   /* BSpec 56640 disallows broadcast of bfloat16 scalar.
    * Insert a MOV to eliminate bfloat scalar broadcast.
    */
   jay_foreach_src(I, s) {
      bool is_bf16 = jay_src_type(I, s) == JAY_TYPE_BF16;
      bool is_broadcast = I->src[s].file == UGPR;
      if (is_bf16 && is_broadcast && I->dst.file != UGPR) {
         jay_builder b = jay_init_builder(f, jay_before_inst(I));
         jay_def tmp = jay_alloc_def(&b, GPR, 1);
         jay_MOV(&b, tmp, I->src[s]);
         jay_replace_src(&I->src[s], tmp);
      }
   }
}

void
jay_lower_pre_ra(jay_shader *s)
{
   struct hash_table_u64 *constants = _mesa_hash_table_u64_create(NULL);

   jay_foreach_function(s, f) {
      /* If we prioritize instruction count, pool constants per function. If we
       * prioritize register pressure, pool per block. This is a heuristic fed
       * by the pressure scheduler. When we have a more competent remat story,
       * this heuristic can hopefully go away but it helps a lot right now.
       */
      _mesa_hash_table_u64_clear(constants);

      jay_foreach_block(f, block) {
         if (f->prioritize_pressure) {
            _mesa_hash_table_u64_clear(constants);
         }

         jay_foreach_inst_in_block(block, I) {
            jay_builder b = { .shader = s, .func = f };

            lower_bf16_restrictions(I, f);

            /* Shuffle(UGPR) can result from copyprop if there's a mismatch
             * between isel and divergence analysis (e.g. because multipolygon
             * is disabled). Legalize.
             */
            if (I->op == JAY_OPCODE_SHUFFLE && I->src[0].file == UGPR) {
               assert(!I->predication);
               I->op = JAY_OPCODE_MOV;
               jay_shrink_sources(I, 1);
            }

            /* lower_immediates must be last since it consumes I */
            lower_contiguous_sources(&b, I);
            b.cursor = f->prioritize_pressure ? jay_before_inst(I) :
                                                jay_before_function(f);
            lower_immediates(&b, I, constants);
         }
      }
   }

   _mesa_hash_table_u64_destroy(constants);
}
