/*
 * Copyright 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include "util/hash_table.h"
#include "util/list.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "util/u_qsort.h"
#include "nir.h"
#include "nir_builder.h"

/* NIR pass to reassociate scalar binary arithmetic.
 *
 * Before running this pass, isub/fsub should be lowered to iadd/fadd.
 * iadd/imin3/imin3/etc should be split into binary operations. If possible, fma
 * should be split to fmul/fadd. This maximizes the number of binary operation
 * chains the pass can reassociate.
 *
 * After running this pass, other passes should be run to get the benefit:
 * constant folding, CSE, algebraic, nir_opt_preamble, copy prop, DCE, etc.
 *
 * How does the algorithm work?
 *
 * We first identify "chains". A chain is a list of (not necessarily unique)
 * sources, where a fixed binary operation is repeatedly applied to reduce the
 * chain. Each intermediate operation must only be used by its parent. In
 * other words, a chain is a linearized expression tree.
 *
 * If we have the NIR:
 *
 *  %5 = iadd %0, %1
 *  %6 = iadd %2, %3
 *  %7 = iadd %5, %6
 *  %8 = iadd %4, %7
 *
 * Then (%0, %1, %2, %3, %4) is a length-5 chain rooted at the last iadd.
 *
 * The sources in each chain are reordered, then we rewrite the program to use
 * our selected order. The chosen order affects how effective other
 * optimizations are. We therefore use two major heuristics.
 *
 * The first heuristic is "sort by rank". Rank is traditionally defined as how
 * "deep" a definition is in the control flow graph. Constants get rank 0,
 * definitions involving 1 level of control flow rank 1, and so on. By
 * operating on low rank sources first, we improve our chances of hoisting
 * low rank operations. Sort-by-rank therefore promotes constant folding,
 * preamble/scalar ALU usage, and loop-invariant code motion.
 *
 * The second heuristic is the "global CSE" heuristic. Pairs of sources might
 * appear in multiple chains. By reordering to perform these common operations
 * first, we are able to CSE inner calculations across chains. This is
 * especially effective for graphics shaders, which often contain code like:
 *
 *    scale * normalize(v)
 *
 * ...scalarizing to
 *
 *    inv_magnitude = rsq(dot(v, v))
 *    scale * (v.x * inv_magnitude)
 *    scale * (v.y * inv_magnitude)
 *    scale * (v.z * inv_magnitude)
 *
 * This scalar code contains three fmul chains:
 *
 *    (scale, v.x, inv_magnitude)
 *    (scale, v.y, inv_magnitude)
 *    (scale, v.z, inv_magnitude)
 *
 * We count the number of appearances of each pair globally:
 *
 *   3 (scale, inv_magnitude)
 *   1 (scale, v.x), (scale, v.y), (scale, v.z)
 *
 * For each chain, the (scale, inv_magnitude) pair has the highest frequency so
 * is performed first, exposing the CSE opportunity:
 *
 *    inv_magnitude = rsq(dot(v, v))
 *    v.x * (scale * inv_magnitude)
 *    v.y * (scale * inv_magnitude)
 *    v.z * (scale * inv_magnitude)
 *
 * References:
 *
 *    Rank heuristic: https://web.eecs.umich.edu/~mahlke/courses/583f22/lectures/Nov14/group19_paper.pdf
 *    CSE heuristic: https://reviews.llvm.org/D40049
 *    LLVM: https://llvm.org/doxygen/Reassociate_8cpp_source.html
 *    GCC: https://github.com/gcc-mirror/gcc/tree/master/gcc/tree-ssa-reassoc.cc
 */

#define MAX_CHAIN_LENGTH   16
#define PASS_FLAG_INTERIOR (1)

struct pair_key {
   /* Def index of each source */
   uint32_t index[2];

   /* Component of each source */
   uint8_t component[2];

   /* Operation applied to the pair. Each operation gets a separate abstract
    * pair map, concretely implemented by including the opcode in the key.
    *
    * nir_op, but uint16_t becuase of MSVC.
    */
   uint16_t op;
};
static_assert(sizeof(struct pair_key) == 12, "packed");

DERIVE_HASH_TABLE(pair_key);

static struct pair_key
get_pair_key(nir_op op, nir_scalar a, nir_scalar b)
{
   /* Normalize pairs for better results, exploiting op's commutativity. */
   if ((a.def->index > b.def->index) ||
       ((a.def->index == b.def->index) && (a.comp > b.comp))) {

      SWAP(a, b);
   }

   return (struct pair_key){
      .index = {a.def->index, b.def->index},
      .component = {a.comp, b.comp},
      .op = op,
   };
}

/*
 * We record the frequency of pairs in a hash table. As a small optimization, we
 * record the frequency (which is always non-zero) in the `data` field directly,
 * without an extra indirection.
 */
static void
increment_pair_freq(struct hash_table *ht, struct pair_key key)
{
   uint32_t hash = pair_key_hash(&key);
   struct hash_entry *ent = _mesa_hash_table_search_pre_hashed(ht, hash, &key);

   if (ent) {
      ent->data = (void *)(((uintptr_t)ent->data) + 1);
   } else {
      struct pair_key *clone = ralloc_memdup(ht, &key, sizeof(key));

      _mesa_hash_table_insert_pre_hashed(ht, hash, clone, (void *)(uintptr_t)1);
   }
}

static unsigned
lookup_pair_freq(struct hash_table *ht, struct pair_key key)
{
   return (uintptr_t)(_mesa_hash_table_search(ht, &key)->data);
}

static int
rank(nir_scalar s)
{
   /* Constants are rank 0. This promotes constant folding. */
   if (nir_scalar_is_const(s))
      return 0;

   /* Convergent expressions are rank 1, promoting preambles and scalar ALU */
   if (!s.def->divergent)
      return 1;

   /* Everything else is rank 2. TODO: Promote loop-invariant code motion. */
   return 2;
}

struct chain {
   nir_alu_instr *root;
   unsigned length;
   nir_scalar srcs[MAX_CHAIN_LENGTH];
   bool do_global_cse, exact;
   unsigned fp_fast_math;
};

UNUSED static void
print_chain(struct chain *c)
{
   for (unsigned i = 0; i < c->length; ++i) {
      printf("%s%u.%c", i ? ", " : "", c->srcs[i].def->index,
             "xyzw"[c->srcs[i].comp]);
   }

   printf("\n");
}

static bool
can_reassociate(nir_alu_instr *alu)
{
   /* By design, we only handle scalar math. */
   if (alu->def.num_components != 1)
      return false;

   /* Check for the relevant algebraic properties. get_pair_key requires
    * commutativity. NIR does not currently have non-commutative associative
    * ALU operations, although that could change.
    */
   nir_op_algebraic_property props = nir_op_infos[alu->op].algebraic_properties;

   return (props & NIR_OP_IS_2SRC_COMMUTATIVE) &&
          ((props & NIR_OP_IS_ASSOCIATIVE) ||
           (!alu->exact && (props & NIR_OP_IS_INEXACT_ASSOCIATIVE)));
}

/*
 * Recursive depth-first-search rooted at a given instruction to build a chain
 * of sources. Effectively, this linearizes expression trees. We cap the search
 * depth with careful accounting to ensure we do not exceed MAX_CHAIN_LENGTH.
 */
static void
build_chain(struct chain *c, nir_scalar def, unsigned reserved_count)
{
   nir_alu_instr *alu = nir_def_as_alu(def.def);

   /* Conservative fast math handling: if ANY instruction along the chain is
    * exact, treat the whole chain as exact. Likewise for float controls.
    *
    * It is safe to add `exact` or float control bits, but not the reverse.
    */
   c->exact |= alu->exact;
   c->fp_fast_math |= alu->fp_fast_math;

   for (unsigned i = 0; i < 2; ++i) {
      nir_scalar src = nir_scalar_chase_alu_src(def, i);
      unsigned remaining = 1 - i;
      unsigned reserved_plus_remaining = reserved_count + remaining;

      if (nir_scalar_is_alu(src) && nir_scalar_alu_op(src) == alu->op &&
          list_is_singular(&src.def->uses) &&
          c->length + reserved_plus_remaining + 2 <= MAX_CHAIN_LENGTH) {

         /* Any interior nodes cannot be the root */
         nir_def_instr(src.def)->pass_flags = PASS_FLAG_INTERIOR;

         /* Recurse, reserving space for the next sources */
         build_chain(c, src, reserved_count + remaining);
      } else {
         assert(c->length < MAX_CHAIN_LENGTH);
         c->srcs[c->length++] = src;
      }
   }
}

/* Iterate all O(N^2) pairs. Since we don't care about order or self-pairs, we
 * start j at (i + 1) to improve runtime.
 */
#define foreach_pair(chain, i, j)                                              \
   for (unsigned i = 0; i < (chain)->length; ++i)                              \
      for (unsigned j = i + 1; j < (chain)->length; ++j)

static void
record_pairs(struct chain *c, struct hash_table *pair_freq)
{
   struct pair_key keys[MAX_CHAIN_LENGTH * MAX_CHAIN_LENGTH];
   unsigned key_count = 0;

   foreach_pair(c, i, j) {
      struct pair_key key = get_pair_key(c->root->op, c->srcs[i], c->srcs[j]);
      bool unique = true;

      /* Deduplicate keys within a chain to avoid bias */
      for (unsigned k = 0; k < key_count; ++k) {
         if (pair_key_equal(&keys[k], &key)) {
            unique = false;
            break;
         }
      }

      /* Increment for unique keys */
      if (unique) {
         increment_pair_freq(pair_freq, key);
         keys[key_count++] = key;
      }
   }
}

/*
 * Search for chains. To do so efficiently, we walk backwards. NIR's source
 * order is compatible with dominance. That guarantees we see roots before
 * interior instructions/leaves. When searching at each potential root, we mark
 * interior nodes as we go, so we know not to consider them for roots. This
 * ensures we do not duplicate chains and keeps `find_chains` O(instructions).
 */
static void
find_chains(nir_function_impl *impl, struct hash_table *pair_freq,
            struct util_dynarray *chains)
{
   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse(instr, block) {
         if (instr->type != nir_instr_type_alu ||
             instr->pass_flags == PASS_FLAG_INTERIOR)
            continue;

         nir_alu_instr *alu = nir_instr_as_alu(instr);
         if (!can_reassociate(alu))
            continue;

         /* Find the chain rooted at `alu` */
         struct chain c = {.root = alu, .length = 0};
         build_chain(&c, nir_get_scalar(&alu->def, 0), 0);

         /* Record pairs even if we won't reassociate this chain, so we get
          * better CSE behaviour globally with other chains.
          */
         if (pair_freq && c.length <= 8)
            record_pairs(&c, pair_freq);

         /* We need at least 3 sources to reassociate anything */
         if (c.length < 3)
            continue;

         /* Analyze the chain to feed our heuristic */
         unsigned lowest_rank = UINT32_MAX, nr_lowest = 0;
         unsigned highest_rank = 0, nr_highest = 0;
         bool local = true;

         for (unsigned i = 0; i < c.length; ++i) {
            lowest_rank = MIN2(rank(c.srcs[i]), lowest_rank);
            highest_rank = MAX2(rank(c.srcs[i]), highest_rank);
            local &= nir_def_block(c.srcs[i].def) == block;
         }

         for (unsigned i = 0; i < c.length; ++i) {
            nr_lowest += (rank(c.srcs[i]) == lowest_rank);
            nr_highest += (rank(c.srcs[i]) == highest_rank);
         }

         /* If we don't have the pair_freq table, the caller doesn't want to use
          * the global CSE heuristic at all.
          */
         c.do_global_cse = pair_freq != NULL;

         /* The global CSE heuristic is quadratic-time in the length of the
          * chain, because it needs to consider all pairs. We limit that
          * heuristic to small chains to keep the worst-case constant-time. Past
          * a point, increasing chain lengths has diminishing returns.
          *
          * Secondarily, this serves to control register pressure. Both
          * reassociating chains and CSE itself tend to increase pressure. This
          * increase is particularly pronounced for chains spanning a large part
          * of the control flow graph. Therefore, we allow longer chains for
          * local chains (where all instructions are in a single basic block)
          * rather than cross-block chains. This trades off instruction count
          * and register pressure, and probably needs to be tuned.
          */
         c.do_global_cse &= c.length <= (local ? 8 : 3);

         /* The heuristic targeting global CSE can interfere with preamble
          * forming, where sort-by-rank excels. For chains where all sources
          * have the same rank except 1, we disable the CSE heuristic and
          * instead sort-by-rank. This is itself a heuristic.
          *
          * As a concrete example, consider the code:
          *
          *    out1 = input1 + uniform1 + uniform2
          *    out2 = input1 + uniform1 + uniform3
          *
          * The global CSE heuristic will associate this code as:
          *
          *    out1 = (input1 + uniform1) + uniform2
          *    out2 = (input1 + uniform1) + uniform3
          *
          * This lets us delete 1 addition by CSE'ing the first add. However,
          * it prevents us from hoisting anything to the preamble, because the
          * result of that CSE'd addition is not uniform.
          *
          * Sort-by-rank instead associates the code:
          *
          *    out1 = input1 + (uniform1 + uniform2)
          *    out2 = input1 + (uniform1 + uniform3)
          *
          * Both uniform-uniform adds get hoisted to the preamble. For the main
          * shader, this is a net reduction in 1 add.
          *
          * For hardware with scalar ALUs but no preambles: the first version
          * costs 3 VALU, the second version costs 2 VALU + 2 SALU. Since SALU
          * is usually underused, that may be a win.
          *
          * For hardware that doesn't have either, this heuristic only affects
          * constants. Enabling constant folding here is a strict win.
          */
         c.do_global_cse &= nr_lowest != (c.length - 1);

         /* If all the ranks are the same, sort-by-rank is pointless */
         bool sort_by_rank = nr_lowest != c.length;

         /* If all ranks are maximal except one, sort-by-rank is unlikely to
          * help much. This is a chain like "scalar + vector + vector", which is
          * 2 vector adds no matter where we put the scalar. Reassociating such
          * a chain is likely to increase register pressure without improving
          * instruction count, so bail. This is a heuristic tradeoff.
          */
         sort_by_rank &= nr_highest != (c.length - 1);

         /* Reassociate the chain if one of our heuristics can improve it */
         if (sort_by_rank || c.do_global_cse) {
            util_dynarray_append(chains, c);
         }
      }
   }
}

struct pair {
   unsigned i, j;
};

/*
 * Find the most frequent pair in a chain. Tie break with the pair with the
 * lowest max rank of the two operands. This is the meat of the CSE heuristic.
 */
static struct pair
find_best_pair_in_chain(struct chain *c, void *pair_freq)
{
   struct pair best = {0};
   unsigned best_max_rank = 0, best_freq = 0;

   foreach_pair(c, i, j) {
      struct pair_key key = get_pair_key(c->root->op, c->srcs[i], c->srcs[j]);
      unsigned freq = lookup_pair_freq(pair_freq, key);
      unsigned max_rank = MAX2(rank(c->srcs[i]), rank(c->srcs[j]));

      if (freq > best_freq || (freq == best_freq && max_rank < best_max_rank)) {
         best = (struct pair){i, j};
         best_max_rank = max_rank;
         best_freq = freq;
      }
   }

   return best_freq > 1 ? best : (struct pair){0};
}

/* Compare ranks. Tie break to ensure the sort-by-rank sort is stable */
static int
cmp_rank(const void *a_, const void *b_)
{
   const nir_scalar *a = a_, *b = b_;
   int ra = rank(*a), rb = rank(*b);

   if (ra != rb)
      return ra - rb;
   else
      return a->def->index - b->def->index;
}

static bool
reassociate_chain(struct chain *c, void *pair_freq)
{
   nir_builder b = nir_builder_at(nir_before_instr(&c->root->instr));
   b.exact = c->exact;
   b.fp_fast_math = c->fp_fast_math;

   /* Pick a new order using sort-by-rank and possibly the CSE heuristics */
   unsigned pinned = 0;

   if (c->do_global_cse) {
      struct pair best_pair = find_best_pair_in_chain(c, pair_freq);

      if (best_pair.i != best_pair.j) {
         /* Pin the best pair at the front. The rest is sorted by rank. */
         SWAP(c->srcs[0], c->srcs[best_pair.i]);
         SWAP(c->srcs[1], c->srcs[best_pair.j]);
         pinned = 2;
      }
   }

   qsort(c->srcs + pinned, c->length - pinned, sizeof(c->srcs[0]), cmp_rank);

   /* Reassociate according to the new order */
   nir_def *new_root = nir_mov_scalar(&b, c->srcs[0]);
   nir_def *last_src = NULL;
   for (unsigned i = 1; i < c->length; ++i) {
      nir_def *src = nir_mov_scalar(&b, c->srcs[i]);

      /* If a source is duplicated in a chain, sort-by-rank groups the
       * duplicates. Associate [x, y, y] as (x + (y + y)) to fuse FMA.
       */
      if (i < c->length - 1 && nir_scalar_equal(c->srcs[i], c->srcs[i + 1])) {
         src = nir_build_alu2(&b, c->root->op, src, src);
         ++i;
      }

      if (i < c->length - 1)
         new_root = nir_build_alu2(&b, c->root->op, new_root, src);
      else
         last_src = src;
   }

   /* It is essential that the root itself is rewritten in place, rather than
    * adding a new instruction and rewriting uses. The root may be used as a
    * source in other chains, and we do all the analysis upfront, so we would
    * get dangling references to the pre-rewrite root.
    *
    * For interior nodes, it doesn't matter, since nothing references them
    * outside the chain by definition. The old instructions will be DCE'd.
    */
   nir_alu_src_rewrite_scalar(&c->root->src[0], nir_get_scalar(last_src, 0));
   nir_alu_src_rewrite_scalar(&c->root->src[1], nir_get_scalar(new_root, 0));

   /* Set flags conservatively, matching the rest of the chain */
   c->root->no_signed_wrap = c->root->no_unsigned_wrap = false;
   c->root->exact = c->exact;
   c->root->fp_fast_math = c->fp_fast_math;
   return true;
}

bool
nir_opt_reassociate(nir_shader *nir, nir_reassociate_options opts)
{
   bool cse_heuristic = opts & nir_reassociate_cse_heuristic;
   struct hash_table *pair_freq =
      cse_heuristic ? pair_key_table_create(NULL) : NULL;
   struct util_dynarray chains;
   bool progress = false;

   /* Clear pass flags. All instructions are possible roots, a priori. Interior
    * nodes are indicated with a non-zero pass flags, set as we go.
    */
   chains = UTIL_DYNARRAY_INIT;
   nir_shader_clear_pass_flags(nir);

   /* We use nir_def indices, which are function-local, so the algorithm runs on
    * one function at a time.
    */
   nir_foreach_function_impl(impl, nir) {
      if (opts & nir_reassociate_scalar_math)
         nir_metadata_require(impl, nir_metadata_divergence);

      nir_index_ssa_defs(impl);

      bool impl_progress = false;
      _mesa_hash_table_clear(pair_freq, NULL);
      util_dynarray_clear(&chains);

      /* Step 1: find all chains in the function */
      find_chains(impl, pair_freq, &chains);

      /* Step 2: reassociate all chains */
      util_dynarray_foreach(&chains, struct chain, chain) {
         impl_progress |= reassociate_chain(chain, pair_freq);
      }

      nir_progress(impl_progress, impl, nir_metadata_control_flow);
      progress |= impl_progress;
   }

   ralloc_free(pair_freq);
   util_dynarray_fini(&chains);
   return progress;
}

/* Helper loop for doing reassociation.
 *
 * You need to do two passes per set of flags you run with, because in the first
 * pass we may reassociate constants together, which can then be folded,
 * resulting in a cleanup, then you need to rebalance again. If you do both
 * heuristics, you want to do the full CSE path first, then finalize with just
 * the scalar math once the CSE work has been done.
 */
bool
nir_opt_reassociate_loop(nir_shader *nir, nir_reassociate_options in_opts)
{
   bool any_progress = false;

   for (unsigned i = 0; i < 2; ++i) {
      nir_reassociate_options opts = in_opts;
      if (i >= 1) {
         if (in_opts == (nir_reassociate_cse_heuristic | nir_reassociate_scalar_math))
            opts = nir_reassociate_scalar_math;
         else
            break;
      }

      /* For this set of opt flags, reassociate, then clean up constant folding,
       * then re-reassociate to rebalance.
       */
      for (unsigned j = 0; j < 2; j++) {
         bool progress = false;
         NIR_PASS(progress, nir, nir_opt_reassociate, opts);
         if (!progress)
            break;

         any_progress = true;

         do {
            progress = false;

            NIR_PASS(progress, nir, nir_opt_algebraic);
            NIR_PASS(progress, nir, nir_opt_constant_folding);
            NIR_PASS(progress, nir, nir_opt_copy_prop);
            NIR_PASS(progress, nir, nir_opt_cse);
            NIR_PASS(progress, nir, nir_opt_dce);
            any_progress |= progress;
         } while (progress);
      }
   }

   return any_progress;
}
