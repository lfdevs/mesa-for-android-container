/*
 * Copyright 2026 Intel Corporation
 * Copyright 2023-2024 Alyssa Rosenzweig
 * Copyright 2023-2024 Valve Corporation
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "util/ralloc.h"
#include "util/sparse_bitset.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "util/u_qsort.h"
#include "util/u_worklist.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * An implementation of "Register Spilling and Live-Range Splitting for SSA-Form
 * Programs" by Braun and Hack.
 *
 * Next-use distances are logically in ℤ ∪ {∞}, modelled as saturating uint32
 * and referred to as dist_t. Within a block, next-use data is dense. At block
 * boundaries, next-use maps are stored as key-value pairs, where only variables
 * with later uses (finite distance) are stored. That sparse representation
 * ensures linear-time even for shaders with many blocks.
 */
#define DIST_INFINITY (UINT32_MAX)
typedef uint32_t dist_t;

struct next_use {
   uint32_t index;
   dist_t dist;
};

static void
add_next_use(struct util_dynarray *nu, unsigned node, dist_t dist)
{
   struct next_use use = { .index = node, .dist = dist };
   util_dynarray_append(nu, use);
}

#define foreach_next_use(nu, it) util_dynarray_foreach(nu, struct next_use, it)

static dist_t
add_dist(dist_t A, dist_t B)
{
   return (A + B < A) ? DIST_INFINITY : (A + B);
}

/*
 * Calculate the minimum of two next-use sets. Values absent from one of the
 * underlying sets are infinity so do not contribute to the minimum, instead
 * acting like a set union.
 */
static bool
minimum_next_uses(struct util_dynarray *nu,
                  const struct util_dynarray *from,
                  dist_t *tmp_dist,
                  struct u_sparse_bitset *tmp_set)
{
   /* Convert "from" to be dense */
   u_sparse_bitset_clear_all(tmp_set);

   foreach_next_use(from, it) {
      u_sparse_bitset_set(tmp_set, it->index);
      tmp_dist[it->index] = it->dist;
   }

   bool progress = false;

   /* Take the minimum of common elements */
   foreach_next_use(nu, it) {
      if (u_sparse_bitset_test(tmp_set, it->index)) {
         if (tmp_dist[it->index] < it->dist) {
            it->dist = tmp_dist[it->index];
            progress = true;
         }

         u_sparse_bitset_clear(tmp_set, it->index);
      }
   }

   /* Add elements that are only in "from" */
   U_SPARSE_BITSET_FOREACH_SET(tmp_set, index) {
      add_next_use(nu, index, tmp_dist[index]);
      progress = true;
   }

   return progress;
}

static uint32_t
inst_cycles(const jay_inst *I)
{
   return 1;
}

struct spill_block {
   /* W/S sets at the start/end of the block, see spill_ctx::{W,S} */
   struct u_sparse_bitset W_in, W_out, S_in, S_out;

   /* Next-use maps at the start/end of the block */
   struct util_dynarray next_use_in, next_use_out;

   /* Estimated cycle count of the block */
   uint32_t cycles;
};

struct spill_ctx {
   jay_function *func;

   /* Set of values whose file equals GPR */
   BITSET_WORD *in_file;

   /* Set of values currently available in the register file */
   struct u_sparse_bitset W;

   /* For W-entry calculation, phis with a spilled source. For
    * coupling calculation, phis defined along the given edge.
    */
   struct u_sparse_bitset phi_set;

   /* |W| = Current register pressure */
   unsigned nW;

   /* For each variable in N, local IPs of next-use. Else, infinite. */
   struct u_sparse_bitset N;
   dist_t *next_uses;

   /* Current local IP relative to the start of the block */
   uint32_t ip;

   /* Set of live values that have been spilled. Contrary to the paper, this
    * is not a subset of W: the definition in the paper is bogus.
    */
   struct u_sparse_bitset S;

   /* If a value is rematerializable or a phi, its definition. Else, NULL */
   jay_inst **defs;

   /* Maximum register pressure allowed */
   unsigned k;

   /* Number of variables */
   unsigned n;

   /* Information on blocks indexed in source order */
   struct spill_block *blocks;

   /* Preallocated array of candidates for calculating W entry */
   struct next_use *candidates;
   struct util_dynarray next_ip;
};

static inline jay_def
jay_def_as_mem(struct spill_ctx *ctx, jay_def idx)
{
   assert(idx.file == GPR);
   idx.file = MEM;
   idx._payload = jay_base_index(idx) + ctx->n;
   return idx;
}

static bool
can_remat(jay_inst *I)
{
   /* TODO */
   return false;
}

static bool
can_remat_node(struct spill_ctx *ctx, unsigned node)
{
   return ctx->defs[node] && ctx->defs[node]->op != JAY_OPCODE_PHI_DST;
}

static jay_inst *
remat_to(jay_builder *b, jay_def dst, struct spill_ctx *ctx, unsigned node)
{
   jay_inst *I = ctx->defs[node];
   assert(can_remat(I));

   UNREACHABLE("invalid remat");
}

static void
insert_spill(jay_builder *b, struct spill_ctx *ctx, unsigned node)
{
   if (!can_remat_node(ctx, node)) {
      jay_def idx = jay_scalar(GPR, node);
      jay_MOV(b, jay_def_as_mem(ctx, idx), idx);
   }
}

static void
insert_reload(struct spill_ctx *ctx,
              jay_block *block,
              jay_cursor cursor,
              unsigned node)
{
   jay_builder b = jay_init_builder(ctx->func, cursor);
   jay_def idx = jay_scalar(GPR, node);

   /* Reloading breaks SSA, but jay_repair_ssa will repair */
   if (can_remat_node(ctx, node)) {
      remat_to(&b, idx, ctx, node);
   } else {
      jay_MOV(&b, idx, jay_def_as_mem(ctx, idx));
   }
}

/* Insert into the register file */
static void
insert_W(struct spill_ctx *ctx, unsigned v)
{
   assert(!u_sparse_bitset_test(&ctx->W, v));
   assert(BITSET_TEST(ctx->in_file, v));

   u_sparse_bitset_set(&ctx->W, v);
   ctx->nW++;
}

/* Remove from the register file */
static void
remove_W(struct spill_ctx *ctx, unsigned v)
{
   assert(u_sparse_bitset_test(&ctx->W, v));
   assert(BITSET_TEST(ctx->in_file, v));

   u_sparse_bitset_clear(&ctx->W, v);
   ctx->nW--;
}

static int
nu_score(struct spill_ctx *ctx, struct next_use nu)
{
   /* We assume that rematerializing - even before every instuction - is
    * cheaper than spilling. As long as one of the nodes is rematerializable
    * (with distance > 0), we choose it over spilling. Within a class of nodes
    * (rematerializable or not), compare by next-use-distance.
    */
   bool remat = can_remat_node(ctx, nu.index) && nu.dist > 0;
   return (remat ? 0 : 100000) + nu.dist;
}

static int
cmp_dist(const void *left_, const void *right_, void *ctx)
{
   const struct next_use *left = left_;
   const struct next_use *right = right_;
   int l = nu_score(ctx, *left), r = nu_score(ctx, *right);

   return (l > r) - (l < r);
}

/*
 * Limit the register file W to maximum size m by evicting registers.
 */
static ATTRIBUTE_NOINLINE void
limit(struct spill_ctx *ctx, jay_inst *I, unsigned m)
{
   /* Nothing to do if we're already below the limit */
   if (ctx->nW <= m) {
      return;
   }

   /* Gather candidates for eviction. Note that next_uses gives IPs whereas
    * cmp_dist expects relative distances. This requires us to subtract ctx->ip
    * to ensure that cmp_dist works properly. Even though logically it shouldn't
    * affect the sorted order, practically this matters for correctness with
    * rematerialization. See the dist=0 test in cmp_dist.
    */
   struct next_use vars[JAY_NUM_UGPR];
   unsigned j = 0;

   U_SPARSE_BITSET_FOREACH_SET(&ctx->W, i) {
      assert(ctx->next_uses[i] != DIST_INFINITY && "live in W");
      dist_t dist = ctx->next_uses[i] - ctx->ip;

      assert(j < ARRAY_SIZE(vars));
      vars[j++] = (struct next_use) { .index = i, .dist = dist };
   }

   /* Sort by next-use distance */
   util_qsort_r(vars, j, sizeof(struct next_use), cmp_dist, ctx);

   /* Evict what doesn't fit, inserting a spill for evicted values that we
    * haven't spilled before with a future use.
    */
   for (unsigned i = m; i < j; ++i) {
      if (!u_sparse_bitset_test(&ctx->S, vars[i].index)) {
         jay_builder b = jay_init_builder(ctx->func, jay_before_inst(I));
         insert_spill(&b, ctx, vars[i].index);
         u_sparse_bitset_set(&ctx->S, vars[i].index);
      }

      remove_W(ctx, vars[i].index);
   }
}

/*
 * Insert coupling code on block boundaries. This must ensure:
 *
 *    - anything live-in we expect to have spilled is spilled
 *    - anything live-in we expect to have filled is filled
 *    - phi sources are spilled if the destination is spilled
 *    - phi sources are filled if the destination is not spilled
 *
 * The latter two requirements ensure correct pressure calculations for phis.
 */
static ATTRIBUTE_NOINLINE void
insert_coupling_code(struct spill_ctx *ctx, jay_block *pred, jay_block *succ)
{
   jay_builder b = jay_init_builder(ctx->func, jay_before_function(ctx->func));
   struct spill_block *sp = &ctx->blocks[pred->index];
   struct spill_block *ss = &ctx->blocks[succ->index];

   /* Insert spills at phi sources to match their destination */
   jay_foreach_phi_src_in_block(pred, phi_src) {
      jay_inst *phi_dst = ctx->defs[jay_phi_src_index(phi_src)];
      unsigned src = jay_index(phi_src->src[0]);

      if (phi_src->src[0].file == GPR && phi_dst->dst.file == MEM) {
         if (!u_sparse_bitset_test(&sp->S_out, src)) {
            /* Spill the phi source. TODO: avoid redundant spills here */
            b.cursor = jay_after_block_logical(pred);
            insert_spill(&b, ctx, src);
         }

         if (can_remat_node(ctx, jay_index(phi_src->src[0]))) {
            jay_def idx = jay_scalar(GPR, src);
            jay_def tmp = jay_alloc_def(&b, GPR, 1);

            b.cursor = jay_before_function(ctx->func);
            remat_to(&b, tmp, ctx, src);
            jay_MOV(&b, jay_def_as_mem(ctx, idx), tmp);
         }

         /* Use the spilled version */
         phi_src->src[0] = jay_def_as_mem(ctx, phi_src->src[0]);
         jay_set_phi_src_index(phi_src, jay_index(phi_dst->dst));
      }
   }

   /* Anything assumed to be spilled in succ must be spilled along all edges. */
   U_SPARSE_BITSET_FOREACH_SET(&ss->S_in, v) {
      if (!u_sparse_bitset_test(&sp->S_out, v)) {
         b.cursor = jay_along_edge(pred, succ, GPR);
         insert_spill(&b, ctx, v);
      }
   }

   jay_foreach_phi_dst_in_block(succ, phi) {
      u_sparse_bitset_set(&ctx->phi_set, jay_index(phi->dst));
   }

   /* Now insert fills at phi sources to match their destination. Note that we
    * must do all spilling before any filling to ensure we stay under the limit.
    */
   jay_foreach_phi_src_in_block(pred, phi_src) {
      unsigned src = jay_index(phi_src->src[0]);

      if (phi_src->src[0].file == GPR &&
          ctx->defs[jay_phi_src_index(phi_src)]->dst.file != MEM &&
          !u_sparse_bitset_test(&sp->W_out, src)) {

         /* Fill the phi source in the predecessor */
         jay_block *reload_block = jay_edge_to_block(pred, succ, GPR);
         insert_reload(ctx, reload_block, jay_along_edge(pred, succ, GPR), src);
      }
   }

   /* Variables in W at the start of succ must be defined along the edge.
    * If not live at the end of the predecessor (and it's not a phi defined in
    * the successor), insert a reload.
    */
   U_SPARSE_BITSET_FOREACH_SET(&ss->W_in, v) {
      if (!u_sparse_bitset_test(&sp->W_out, v) &&
          !u_sparse_bitset_test(&ctx->phi_set, v)) {

         jay_block *reload_block = jay_edge_to_block(pred, succ, GPR);
         insert_reload(ctx, reload_block, jay_along_edge(pred, succ, GPR), v);
      }
   }
}

static dist_t
lookup_next_use(struct spill_ctx *ctx, unsigned v)
{
   return u_sparse_bitset_test(&ctx->N, v) ? ctx->next_uses[v] : DIST_INFINITY;
}

/*
 * Produce an array of next-use IPs relative to the start of the block. This is
 * an array of dist_t scalars, representing the next-use IP of each SSA dest
 * (right-to-left) and SSA source (left-to-right) of each instuction in the
 * block (bottom-to-top). Its size equals the # of SSA sources in the block.
 */
static ATTRIBUTE_NOINLINE void
populate_local_next_use(struct spill_ctx *ctx, jay_block *block)
{
   struct spill_block *sb = &ctx->blocks[block->index];
   unsigned ip = sb->cycles;

   foreach_next_use(&sb->next_use_out, it) {
      dist_t d = add_dist(it->dist, ip);

      if (d != DIST_INFINITY) {
         u_sparse_bitset_set(&ctx->N, it->index);
         ctx->next_uses[it->index] = d;
      }
   }

   jay_foreach_inst_in_block_rev(block, I) {
      ip -= inst_cycles(I);

      jay_foreach_src_index(I, s, c, v) {
         if (I->src[s].file == GPR) {
            if (I->op != JAY_OPCODE_PHI_SRC) {
               util_dynarray_append(&ctx->next_ip, lookup_next_use(ctx, v));
            }

            ctx->next_uses[v] = ip;
            u_sparse_bitset_set(&ctx->N, v);
         }
      }

      if (I->dst.file == GPR) {
         jay_foreach_index_rev(I->dst, _, v) {
            util_dynarray_append(&ctx->next_ip, lookup_next_use(ctx, v));
         }
      }
   }

   assert(ip == 0 && "cycle counting is consistent");
}

/*
 * Insert spills/fills for a single basic block, following Belady's algorithm.
 * Corresponds to minAlgorithm from the paper.
 */
static ATTRIBUTE_NOINLINE void
min_algorithm(struct spill_ctx *ctx,
              jay_block *block,
              struct spill_block *sb,
              dist_t *next_ips,
              unsigned next_use_cursor)
{
   jay_foreach_inst_in_block(block, I) {
      assert(ctx->nW <= ctx->k && "invariant");

      /* Phis are special since they happen along the edge. When we initialized
       * W and S, we implicitly chose which phis are spilled. So, here we just
       * need to rewrite the phis to write into memory.
       *
       * Phi sources are handled later.
       */
      if (I->op == JAY_OPCODE_PHI_DST) {
         if (I->dst.file == GPR) {
            if (!u_sparse_bitset_test(&ctx->W, jay_index(I->dst))) {
               u_sparse_bitset_set(&ctx->S, jay_index(I->dst));
               I->dst = jay_def_as_mem(ctx, I->dst);
            }
         }

         ctx->ip += inst_cycles(I);
         continue;
      } else if (I->op == JAY_OPCODE_PHI_SRC) {
         break;
      }

      /* Any source that is not in W needs to be reloaded. Gather the set R of
       * such values, and add them to the register file.
       */
      unsigned R[JAY_MAX_SRCS], nR = 0;

      jay_foreach_src_index(I, s, c, v) {
         if (I->src[s].file == GPR && !u_sparse_bitset_test(&ctx->W, v)) {
            R[nR++] = v;
            insert_W(ctx, v);

            assert(u_sparse_bitset_test(&ctx->S, v) && "must have spilled");
            assert(nR <= ARRAY_SIZE(R) && "maximum source count");
         }
      }

      /* Limit W to make space for the operands.
       *
       * We need to round up to power-of-two destination sizes to match the
       * rounding in demand calculation.
       */
      bool has_dst = I->dst.file == GPR;
      unsigned dst_size = util_next_power_of_two(jay_num_values(I->dst));
      limit(ctx, I, ctx->k - (has_dst ? dst_size : 0));

      /* Add destinations to the register file */
      if (I->dst.file == GPR) {
         jay_foreach_index(I->dst, _, index) {
            assert(next_use_cursor >= 1);
            ctx->next_uses[index] = next_ips[--next_use_cursor];

            if (ctx->next_uses[index] != DIST_INFINITY) {
               insert_W(ctx, index);
            }
         }
      }

      /* Update next-use distances for this instuction. Unlike the paper, we
       * require W contain only live values (with finite next-use distance).
       *
       * This happens after the above limit() calls to model sources as
       * late-kill. This is conservative and could be improved, but it matches
       * how we currently estimate register demand.
       */
      jay_foreach_src_index_rev(I, s, c, node) {
         if (I->src[s].file == GPR) {
            assert(next_use_cursor >= 1);
            ctx->next_uses[node] = next_ips[--next_use_cursor];

            if (ctx->next_uses[node] == DIST_INFINITY) {
               remove_W(ctx, node);
            }
         }
      }

      /* Add reloads for the sources in front of the instuction. */
      for (unsigned i = 0; i < nR; ++i) {
         insert_reload(ctx, block, jay_before_inst(I), R[i]);
      }

      ctx->ip += inst_cycles(I);

      if (jay_debug & JAY_DBG_PRINTDEMAND) {
         printf("(SP) %u: ", ctx->nW);
         jay_print_inst(stdout, I);
      }
   }

   assert(next_use_cursor == 0 && "exactly sized");

   u_sparse_bitset_dup(&sb->W_out, &ctx->W);
   u_sparse_bitset_dup(&sb->S_out, &ctx->S);
}

/*
 * TODO: Implement section 4.2 of the paper.
 *
 * For now, we implement the simpler heuristic in Hack's thesis: sort
 * the live-in set (+ destinations of phis) by next-use distance.
 */
static ATTRIBUTE_NOINLINE void
compute_w_entry_loop_header(struct spill_ctx *ctx, jay_block *block)
{
   unsigned j = 0;
   foreach_next_use(&ctx->blocks[block->index].next_use_in, it) {
      assert(j < ctx->n);
      ctx->candidates[j++] = *it;
   }

   jay_foreach_phi_dst_in_block(block, I) {
      if (I->dst.file == GPR) {
         ctx->candidates[j++] = (struct next_use) {
            .index = jay_index(I->dst),
            .dist = ctx->next_uses[jay_index(I->dst)],
         };
      }
   }

   /* Take the best candidates sorted by next-use distance */
   unsigned n = MIN2(j, ctx->k - ctx->nW);
   if (n < j) {
      util_qsort_r(ctx->candidates, j, sizeof(struct next_use), cmp_dist, ctx);
   }

   for (unsigned i = 0; i < n; ++i) {
      insert_W(ctx, ctx->candidates[i].index);
   }
}

/*
 * Compute W_entry for a block. Section 4.2 in the paper.
 */
static ATTRIBUTE_NOINLINE void
compute_w_entry(struct spill_ctx *ctx, jay_block *block)
{
   unsigned j = 0;

   /* Variables that are in all predecessors are assumed in W_entry. Phis and
    * variables in some predecessors are scored by next-use.
    */
   U_SPARSE_BITSET_FOREACH_SET(&ctx->N, i) {
      bool all = true, any = false;

      jay_foreach_predecessor(block, P, GPR) {
         bool in = u_sparse_bitset_test(&ctx->blocks[(*P)->index].W_out, i);
         all &= in;
         any |= in;
      }

      if (all) {
         insert_W(ctx, i);
      } else if (any) {
         ctx->candidates[j++] =
            (struct next_use) { .index = i, .dist = ctx->next_uses[i] };
      }
   }

   jay_foreach_predecessor(block, pred, GPR) {
      jay_foreach_phi_src_in_block(*pred, I) {
         if (!u_sparse_bitset_test(&ctx->blocks[(*pred)->index].W_out,
                                   jay_index(I->src[0]))) {

            u_sparse_bitset_set(&ctx->phi_set, jay_phi_src_index(I));
         }
      }
   }

   /* Heuristic: if any phi source is spilled, spill the phi. While suboptimal,
    * this reduces pointless spills/fills with massive phi webs.
    */
   jay_foreach_phi_dst_in_block(block, I) {
      if (I->dst.file == GPR &&
          !u_sparse_bitset_test(&ctx->phi_set, jay_index(I->dst))) {
         ctx->candidates[j++] = (struct next_use) {
            .index = jay_index(I->dst),
            .dist = ctx->next_uses[jay_index(I->dst)],
         };
      }
   }

   /* Take the best candidates sorted by next-use distance */
   unsigned n = MIN2(j, ctx->k - ctx->nW);
   if (n < j) {
      util_qsort_r(ctx->candidates, j, sizeof(struct next_use), cmp_dist, ctx);
   }

   for (unsigned i = 0; i < n; ++i) {
      insert_W(ctx, ctx->candidates[i].index);
   }
}

/*
 * We initialize S with the union of S at the exit of (forward edge)
 * predecessors and the complement of W, intersected with the live-in set. The
 * former propagates S forward. The latter ensures we spill along the edge when
 * a live value is not selected for the entry W.
 */
static ATTRIBUTE_NOINLINE void
compute_s_entry(struct spill_ctx *ctx, jay_block *block)
{
   jay_foreach_predecessor(block, pred, GPR) {
      U_SPARSE_BITSET_FOREACH_SET(&ctx->blocks[(*pred)->index].S_out, v) {
         if (u_sparse_bitset_test(&block->live_in, v)) {
            u_sparse_bitset_set(&ctx->S, v);
         }
      }
   }

   U_SPARSE_BITSET_FOREACH_SET(&block->live_in, v) {
      if (BITSET_TEST(ctx->in_file, v) && !u_sparse_bitset_test(&ctx->W, v)) {
         u_sparse_bitset_set(&ctx->S, v);
      }
   }

   u_sparse_bitset_dup(&ctx->blocks[block->index].S_in, &ctx->S);
}

static ATTRIBUTE_NOINLINE void
global_next_use_distances(struct spill_ctx *ctx, void *memctx)
{
   u_worklist worklist;
   u_worklist_init(&worklist, ctx->func->num_blocks, NULL);

   jay_foreach_block(ctx->func, block) {
      struct spill_block *sb = &ctx->blocks[block->index];

      util_dynarray_init(&sb->next_use_in, memctx);
      util_dynarray_init(&sb->next_use_out, memctx);

      jay_foreach_inst_in_block(block, I) {
         sb->cycles += inst_cycles(I);
      }

      jay_worklist_push_head(&worklist, block);
   }

   /* Iterate the work list in reverse order since liveness is backwards */
   while (!u_worklist_is_empty(&worklist)) {
      jay_block *block = jay_worklist_pop_head(&worklist);
      struct spill_block *sb = &ctx->blocks[block->index];

      /* Clear locally accessed set (W) */
      u_sparse_bitset_clear_all(&ctx->W);
      util_dynarray_clear(&sb->next_use_in);

      uint32_t cycle = 0;

      /* Calculate dists */
      jay_foreach_inst_in_block(block, I) {
         /* Record first use before def */
         jay_foreach_src_index(I, s, c, index) {
            if (I->src[s].file == GPR &&
                !u_sparse_bitset_test(&ctx->W, index)) {

               add_next_use(&sb->next_use_in, index, cycle);
               u_sparse_bitset_set(&ctx->W, index);
            }
         }

         /* Record defs */
         jay_foreach_index(I->dst, _, index) {
            u_sparse_bitset_set(&ctx->W, index);
         }

         cycle += inst_cycles(I);
      }

      /* Apply transfer function to get our entry state. */
      foreach_next_use(&sb->next_use_out, it) {
         if (!u_sparse_bitset_test(&ctx->W, it->index)) {
            add_next_use(&sb->next_use_in, it->index,
                         add_dist(it->dist, sb->cycles));
         }
      }

      /* Propagate successor live-in to pred live-out, joining with min */
      jay_foreach_predecessor(block, pred, GPR) {
         if (minimum_next_uses(&ctx->blocks[(*pred)->index].next_use_out,
                               &sb->next_use_in, ctx->next_uses,
                               &ctx->phi_set)) {
            jay_worklist_push_tail(&worklist, *pred);
         }
      }
   }

   u_worklist_fini(&worklist);

#ifndef NDEBUG
   /* In debug builds, validate the following invariant:
    *
    * Next-use distance is finite iff live and in file.
    */
   jay_foreach_block(ctx->func, blk) {
      struct spill_block *sb = &ctx->blocks[blk->index];

      for (unsigned i = 0; i < 2; i++) {
         struct util_dynarray *nu = i ? &sb->next_use_out : &sb->next_use_in;
         struct u_sparse_bitset *live = i ? &blk->live_out : &blk->live_in;

         u_sparse_bitset_clear_all(&ctx->W);

         foreach_next_use(nu, it) {
            assert(u_sparse_bitset_test(live, it->index) &&
                   BITSET_TEST(ctx->in_file, it->index));

            u_sparse_bitset_set(&ctx->W, it->index);
         }

         U_SPARSE_BITSET_FOREACH_SET(live, i) {
            if (BITSET_TEST(ctx->in_file, i)) {
               assert(u_sparse_bitset_test(&ctx->W, i));
            }
         }
      }
   }
#endif
}

void
jay_spill(jay_function *func, unsigned k)
{
   void *memctx = ralloc_context(NULL);
   void *linctx = linear_context(memctx);
   struct spill_ctx ctx = { .func = func, .k = k };

   ctx.n = func->ssa_alloc;
   ctx.in_file = BITSET_LINEAR_ZALLOC(linctx, ctx.n);
   ctx.defs = linear_zalloc_array(linctx, jay_inst *, ctx.n);
   ctx.next_uses = linear_alloc_array(linctx, dist_t, ctx.n);
   ctx.candidates = linear_alloc_array(linctx, struct next_use, ctx.n);
   ctx.blocks =
      linear_zalloc_array(linctx, struct spill_block, func->num_blocks);

   jay_foreach_inst_in_func(func, block, I) {
      if (can_remat(I) || I->op == JAY_OPCODE_PHI_DST) {
         ctx.defs[jay_index(I->dst)] = I;
      }

      if (I->dst.file == GPR) {
         BITSET_SET_COUNT(ctx.in_file, jay_base_index(I->dst),
                          jay_num_values(I->dst));
      }
   }

   u_sparse_bitset_init(&ctx.W, ctx.n, memctx);
   u_sparse_bitset_init(&ctx.S, ctx.n, memctx);
   u_sparse_bitset_init(&ctx.N, ctx.n, memctx);
   u_sparse_bitset_init(&ctx.phi_set, ctx.n, memctx);
   util_dynarray_init(&ctx.next_ip, memctx);

   global_next_use_distances(&ctx, memctx);

   /* Reserve a memory variable for every regular variable */
   func->ssa_alloc *= 2;

   jay_foreach_block(func, block) {
      ctx.nW = 0;
      ctx.ip = 0;

      u_sparse_bitset_clear_all(&ctx.W);
      u_sparse_bitset_clear_all(&ctx.S);
      u_sparse_bitset_clear_all(&ctx.N);
      util_dynarray_clear(&ctx.next_ip);

      populate_local_next_use(&ctx, block);

      struct spill_block *sb = &ctx.blocks[block->index];
      dist_t *next_ips = util_dynarray_element(&ctx.next_ip, dist_t, 0);
      unsigned nu_cursor = util_dynarray_num_elements(&ctx.next_ip, dist_t);

      /* Populate next-use with phi destinations, which are not in the
       * next_use_in set but are accounted for when computing W_entry.
       */
      jay_foreach_phi_dst_in_block(block, I) {
         if (I->dst.file == GPR) {
            assert(nu_cursor >= 1);
            ctx.next_uses[jay_index(I->dst)] = next_ips[--nu_cursor];
            u_sparse_bitset_set(&ctx.N, jay_index(I->dst));
         }
      }

      if (block->loop_header) {
         compute_w_entry_loop_header(&ctx, block);
      } else if (jay_num_predecessors(block, GPR) /* skip start blocks */) {
         compute_w_entry(&ctx, block);
      }

      assert(ctx.nW <= ctx.k && "invariant");
      u_sparse_bitset_dup(&sb->W_in, &ctx.W);

      compute_s_entry(&ctx, block);
      min_algorithm(&ctx, block, sb, next_ips, nu_cursor);
   }

   /* Now that all blocks are processed separately, stitch it together */
   jay_foreach_block(func, block) {
      jay_foreach_predecessor(block, pred, GPR) {
         u_sparse_bitset_clear_all(&ctx.phi_set);
         insert_coupling_code(&ctx, *pred, block);
      }
   }

   ralloc_free(memctx);

   /* Spilling breaks SSA, so we need to repair before validating */
   jay_repair_ssa(func);
   jay_validate(func->shader, "Spilling");

   /* Remat can introduce dead code */
   jay_opt_dead_code(func->shader);
}
