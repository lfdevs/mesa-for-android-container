/*
 * Copyright 2026 Intel Corporation
 * Copyright 2025 Valve Corporation
 * Copyright 2019-2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "util/u_worklist.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

#define JAY_MAX_ACCUMS 4

static void
postra_liveness_ins(BITSET_WORD *live, jay_inst *I)
{
   if (I->dst.file == GPR && !I->predication) {
      BITSET_CLEAR_COUNT(live, I->dst.reg, jay_num_values(I->dst));
   }

   jay_foreach_src(I, s) {
      if (I->src[s].file == GPR) {
         BITSET_SET_COUNT(live, I->src[s].reg, jay_num_values(I->src[s]));
      }
   }
}

/*
 * Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */
static void
postra_liveness(jay_function *func)
{
   u_worklist worklist;
   u_worklist_init(&worklist, func->num_blocks, NULL);

   jay_foreach_block(func, block) {
      BITSET_ZERO(block->postra_gpr_live_in);
      BITSET_ZERO(block->postra_gpr_live_out);

      jay_worklist_push_tail(&worklist, block);
   }

   while (!u_worklist_is_empty(&worklist)) {
      /* Pop off in reverse order since liveness is backwards */
      jay_block *blk = jay_worklist_pop_tail(&worklist);

      /* Calculate liveness locally */
      jay_foreach_successor(blk, succ, GPR) {
         BITSET_OR(blk->postra_gpr_live_out, blk->postra_gpr_live_out,
                   succ->postra_gpr_live_in);
      }

      BITSET_DECLARE(live, JAY_NUM_PHYS_GRF);
      memcpy(live, blk->postra_gpr_live_out, sizeof(live));

      jay_foreach_inst_in_block_rev(blk, ins) {
         postra_liveness_ins(live, ins);
      }

      /* If we made progress, we need to reprocess the predecessors */
      if (!BITSET_EQUAL(blk->postra_gpr_live_in, live)) {
         memcpy(blk->postra_gpr_live_in, live, sizeof(live));

         jay_foreach_predecessor(blk, pred, GPR) {
            jay_worklist_push_head(&worklist, *pred);
         }
      }
   }

   u_worklist_fini(&worklist);
}

/*
 * Check if a source is killed by the instruction. If the register is dead after
 * this instuction, it's the last use / killed.  That also includes if the
 * register is overwritten this cycle, but that won't show up in the liveness.
 */
static bool
source_killed(BITSET_WORD *live, const jay_inst *I, unsigned s)
{
   return !BITSET_TEST(live, I->src[s].reg) ||
          (I->dst.file == GPR &&
           I->src[s].reg >= I->dst.reg &&
           (I->src[s].reg - I->dst.reg) < jay_num_values(I->dst));
}

/* We assign accumulators with a simple heuristic: promote registers with the
 * shortest live range. This is pretty naive but it is well-motivated:
 *
 * 1. Short live ranges reduce interfere with other potentially promotable
 *    registers, allowing for more overall accumulator usage. This is a builtin
 *    defense against being too greedy.
 *
 * 2. Short live ranges necessarily have the first read of the register shortly
 *    after the write. That situation benefits greatly from promoting to an
 *    accumulator as such sequences are GRF latency bound.
 *
 * There are lots of ways to do better in the future, but this is good for now.
 */
struct candidate {
   uint32_t def_ip, last_use_ip;
};

static int
score(struct candidate c)
{
   assert(c.def_ip < c.last_use_ip);
   return (int) (c.last_use_ip - c.def_ip);
}

static int
cmp_candidates(const void *left_, const void *right_)
{
   const struct candidate *left = left_;
   const struct candidate *right = right_;
   int l = score(*left), r = score(*right);

   return (l > r) - (l < r);
}

/*
 * Query whether an instruction can access accumulators. Comments are quoted
 * from bspec 56619 as the rules are complex.
 */
static inline bool
can_access_accum(jay_shader *shader, jay_inst *I, signed src)
{
   /* "No Accumulator usage for Control Flow, Math, Send, DPAS instructions." */
   if (jay_op_is_control_flow(I->op) ||
       I->op == JAY_OPCODE_MATH ||
       I->op == JAY_OPCODE_SEND) {
      return false;
   }

   /* TODO: Many, many more restrictions on non-f32 */
   if (I->type != JAY_TYPE_F32) {
      return false;
   }

   /* "When destination is accumulator with offset 0, destination horizontal
    * stride must be 1."
    */
   if (I->dst.file == GPR && jay_def_stride(shader, I->dst) != JAY_STRIDE_4) {
      return false;
   }

   /* "Register Regioning patterns where register data bit locations are changed
    * between source and destination are not supported when an accumulator is
    * used as an implicit source or an explicit source in an instruction.."
    */
   jay_foreach_src(I, s) {
      if (I->src[s].file == GPR &&
          jay_def_stride(shader, I->src[s]) != JAY_STRIDE_4) {
         return false;
      }
   }

   /* Jay's predication requires tying the destination to the source which is
    * too complicated to model here. It's also only dubiously useful.
    */
   if (src < 0 && I->predication) {
      return false;
   }

   /* This copies only part of a GRF so can't be accumulatored */
   if (I->op == JAY_OPCODE_DESWIZZLE_EVEN) {
      return false;
   }

   return true;
}

static inline void
substitute_acc(jay_def *x, unsigned acc_p1)
{
   if (acc_p1) {
      assert(x->file == GPR && (acc_p1 - 1) < JAY_MAX_ACCUMS);

      x->file = ACCUM;
      x->reg = (acc_p1 - 1) * 2;
   }
}

static void
pass(jay_function *func)
{
   void *memctx = ralloc_context(NULL);
   void *linctx = linear_context(memctx);

   /* Analyze the shader globally */
   postra_liveness(func);
   struct util_dynarray candidates = UTIL_DYNARRAY_INIT;

   /* Find the longest block so we can size our allocations & count IPs */
   uint32_t ip_bound = 0;
   jay_foreach_block(func, block) {
      ip_bound = MAX2(ip_bound, list_length(&block->instructions) + 1);
   }

   /* in_use[acc][IP] set if acc is in-use /before/ executing instruction IP */
   BITSET_WORD *in_use[JAY_MAX_ACCUMS];
   unsigned nr_accums = func->shader->dispatch_width == 32 ? 2 : 4;

   for (unsigned i = 0; i < nr_accums; ++i) {
      in_use[i] = BITSET_LINEAR_ZALLOC(linctx, ip_bound);
   }

   /* acc+1 if the instruction writes acc, 0 if no accumulator written */
   uint8_t *ra = linear_zalloc_array(linctx, uint8_t, ip_bound);

   jay_foreach_block(func, block) {
      util_dynarray_clear(&candidates);

      /* Live-set at each point in the program */
      BITSET_DECLARE(live, JAY_NUM_PHYS_GRF);
      memcpy(live, block->postra_gpr_live_out, sizeof(live));

      uint32_t ip = ip_bound;
      uint32_t last_use_ip[JAY_NUM_PHYS_GRF] = { 0 };
      uint32_t pre_live = 0;

      jay_foreach_inst_in_block_rev(block, I) {
         --ip;
         assert(ip > 0 && "invariant");

         /* Collect candidates */
         if (I->dst.file == GPR && last_use_ip[I->dst.reg]) {
            if (can_access_accum(func->shader, I, -1)) {
               struct candidate c = { ip, last_use_ip[I->dst.reg] };
               util_dynarray_append(&candidates, c);
            }

            last_use_ip[I->dst.reg] = 0;
         }

         if (I->dst.file == ACCUM || I->dst.file == UACCUM) {
            pre_live &= ~BITFIELD_BIT(I->dst.reg / 2);
         }

         jay_foreach_src(I, s) {
            if (I->src[s].file == GPR && source_killed(live, I, s)) {
               last_use_ip[I->src[s].reg] = ip;
            }
         }

         /* Prune candidates (in a second loop in case of duplicated sources) */
         jay_foreach_src(I, s) {
            if (I->src[s].file == GPR &&
                !can_access_accum(func->shader, I, s)) {

               jay_foreach_comp(I->src[s], c) {
                  last_use_ip[I->src[s].reg + c] = 0;
               }
            }

            if (I->src[s].file == ACCUM || I->src[s].file == UACCUM) {
               pre_live |= BITFIELD_BIT(I->src[s].reg / 2);
            }
         }

         u_foreach_bit(i, pre_live) {
            BITSET_SET(in_use[i], ip);
         }

         /* Implicit use of the integer accumulator acc0 corrupts acc0/acc1,
          * which coresponds to virtual acc0 in SIMD32 mode (a pair) or virtual
          * acc0/acc1 in SIMD16 (two registers). Model interference.
          */
         if (I->op == JAY_OPCODE_MUL_32) {
            unsigned n = func->shader->dispatch_width < 32 ? 2 : 1;

            for (unsigned i = 0; i < n; ++i) {
               BITSET_SET(in_use[i], ip);
            }
         }

         postra_liveness_ins(live, I);
      }

      qsort(candidates.data,
            util_dynarray_num_elements(&candidates, struct candidate),
            sizeof(struct candidate), cmp_candidates);

      /* Greedily assign candidates */
      util_dynarray_foreach(&candidates, struct candidate, c) {
         for (unsigned i = 0; i < nr_accums; ++i) {
            if (!BITSET_TEST_RANGE(in_use[i], c->def_ip + 1, c->last_use_ip)) {
               BITSET_SET_RANGE(in_use[i], c->def_ip + 1, c->last_use_ip);
               ra[c->def_ip] = i + 1;
               break;
            }
         }
      }

      uint32_t min_ip = ip;
      uint8_t gpr_to_acc_p1[JAY_NUM_PHYS_GRF] = { 0 };

      jay_foreach_inst_in_block_safe(block, I) {
         /* Rewrite operands using accumulators */
         jay_foreach_src(I, s) {
            if (I->src[s].file == GPR) {
               substitute_acc(&I->src[s], gpr_to_acc_p1[I->src[s].reg]);
            }
         }

         if (I->dst.file == GPR) {
            jay_foreach_comp(I->dst, c) {
               gpr_to_acc_p1[I->dst.reg + c] = ra[ip];
            }

            substitute_acc(&I->dst, ra[ip]);
         }

         /* Rewrite MAD->MAC where possible to improve code density.
          *
          * The bspec says "Instructions that specify an implicit accumulator
          * source cannot specify an explicit accumulator source operand.". But
          * it works fine on Lunar Lake so ¯\_(ツ)_/¯
          */
         if ((I->op == JAY_OPCODE_MAD && I->type == JAY_TYPE_F32) &&
             (I->src[2].file == ACCUM && I->src[2].reg == 0) &&
             !(I->src[2].negate || I->src[2].abs)) {

            I->op = JAY_OPCODE_MAC;
         }

         /* Sometimes this algorithm turns nontrivial GPR->GPR copies into
          * trivial accumulator->accumulator copies, which can be coalesced now.
          */
         if (I->op == JAY_OPCODE_MOV && jay_regs_equal(I->dst, I->src[0])) {
            jay_remove_instruction(I);
         }

         ++ip;
      }

      assert(ip == ip_bound);

      /* Zero per-block allocation */
      for (unsigned i = 0; i < nr_accums; ++i) {
         BITSET_CLEAR_RANGE(in_use[i], min_ip, ip);
      }

      memset(ra + min_ip, 0, (ip - min_ip) * sizeof(*ra));
   }

   util_dynarray_fini(&candidates);
   ralloc_free(memctx);
}

JAY_DEFINE_FUNCTION_PASS(jay_assign_accumulators, pass)
