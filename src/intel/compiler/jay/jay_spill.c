/*
 * Copyright 2026 Intel Corporation
 * Copyright 2023-2024 Alyssa Rosenzweig
 * Copyright 2023-2024 Valve Corporation
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/sparse_bitset.h"
#include "util/u_dynarray.h"
#include "util/u_qsort.h"
#include "util/u_worklist.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * Adapted from of "Register Spilling and Live-Range Splitting for SSA-Form
 * Programs" by Braun and Hack and "Simple and Efficient Construction of Static
 * Single Assignment Form" by Braun et al.
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
   /* W set at the block end or (for loop headers) start, see spill_ctx::W */
   struct u_sparse_bitset W_out, W_in;

   /* Next-use maps at the start/end of the block */
   struct util_dynarray next_use_in, next_use_out;

   /* Estimated cycle count of the block */
   uint32_t cycles;

   /* Map of 32-bit indices to remapped 32-bit indices */
   struct hash_table_u64 *remap;
};

struct spill_ctx {
   jay_function *func;

   /* Register file being spilled */
   enum jay_file file;

   /* Set of values whose file equals file */
   BITSET_WORD *in_file;

   /* Set of values currently available in the register file */
   struct u_sparse_bitset W;

   /* |W| = Current register pressure */
   unsigned nW;

   /* For each variable in N, local IPs of next-use. Else, infinite. */
   struct u_sparse_bitset N;
   dist_t *next_uses;

   /* Current local IP relative to the start of the block */
   uint32_t ip;

   /* Set of rematerializable values */
   BITSET_WORD *remat;

   /* Map from values to definitions */
   jay_inst **defs;

   /* For values that have not yet been spilled, map to the block of their
    * definition where a spill will be inserted. NULL for spilled values.
    */
   jay_block **spill_block;

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

static uint32_t
lookup_remap(struct hash_table_u64 *remap, unsigned node)
{
   return (uintptr_t) _mesa_hash_table_u64_search(remap, node);
}

static void
remap_inst(struct spill_ctx *ctx, struct hash_table_u64 *remap, jay_inst *I)
{
   jay_foreach_src(I, s) {
      if (I->src[s].file == ctx->file) {
         jay_foreach_index(I->src[s], c, index) {
            uint32_t new_idx = lookup_remap(remap, index);
            if (new_idx) {
               jay_builder b = jay_init_builder(ctx->func, jay_before_inst(I));
               jay_insert_channel_index(&b, &I->src[s], c, new_idx);
            }
         }
      }
   }
}

static inline jay_def
jay_def_spilled(struct spill_ctx *ctx, unsigned index)
{
   return jay_scalar(ctx->file == UGPR ? GPR : MEM, index + ctx->n);
}

static bool
can_remat(jay_inst *I)
{
   return I->op == JAY_OPCODE_MOV &&
          jay_is_imm(I->src[0]) &&
          !jay_uses_flag(I) &&
          jay_num_values(I->dst) == 1;
}

static void
ensure_spilled(struct spill_ctx *ctx, unsigned node)
{
   if (ctx->spill_block[node]) {
      jay_cursor cursor = jay_op_starts_block(ctx->defs[node]->op) ?
                             jay_before_block(ctx->spill_block[node]) :
                             jay_after_inst(ctx->defs[node]);

      if (!BITSET_TEST(ctx->remat, node)) {
         jay_builder b = jay_init_builder(ctx->func, cursor);
         jay_MOV(&b, jay_def_spilled(ctx, node), jay_scalar(ctx->file, node));
      }

      ctx->spill_block[node] = NULL;
   }
}

static void
insert_reload(struct spill_ctx *ctx,
              jay_block *block,
              jay_cursor cursor,
              unsigned node)
{
   jay_builder b = jay_init_builder(ctx->func, cursor);
   jay_def new_def = jay_alloc_def(&b, ctx->file, 1);

   if (BITSET_TEST(ctx->remat, node)) {
      jay_inst *I = ctx->defs[node];
      assert(can_remat(I));

      jay_inst *clone = jay_clone_inst(&b, I, I->num_srcs);
      clone->dst = new_def;
      jay_builder_insert(&b, clone);
   } else {
      ensure_spilled(ctx, node);
      jay_MOV(&b, new_def, jay_def_spilled(ctx, node));
   }

   _mesa_hash_table_u64_insert(ctx->blocks[block->index].remap, node,
                               (void *) (uintptr_t) jay_index(new_def));
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
   bool remat = BITSET_TEST(ctx->remat, nu.index) && nu.dist > 0;
   return (remat ? 100000 : 0) + nu.dist;
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
 * Limit the register file W to maximum size m by evicting one register at a
 * time (like a selection sort). The routine is O(|W|) since we only evict O(1)
 * registers. This is simpler than quicksort.
 *
 * Note that next_uses gives IPs whereas nu_score expects relative distances, so
 * we subtract ctx->ip.  While it shouldn't affect the sorted order, it ensures
 * correctness with rematerialization.
 */
static ATTRIBUTE_NOINLINE void
limit(struct spill_ctx *ctx, jay_inst *I, unsigned m)
{
   while (ctx->nW > m) {
      int best_score = INT32_MIN, best_i = -1;

      U_SPARSE_BITSET_FOREACH_SET(&ctx->W, i) {
         assert(ctx->next_uses[i] != DIST_INFINITY && "live in W");
         dist_t dist = ctx->next_uses[i] - ctx->ip;

         struct next_use nu = { .index = i, .dist = dist };
         int score = nu_score(ctx, nu);

         if (score > best_score) {
            best_score = score;
            best_i = i;
         }
      }

      assert(best_i >= 0 && "must find something");
      remove_W(ctx, best_i);
   }
}

/*
 * Insert fills along edges for values in the successor's register file W absent
 * in the predecessor (excluding phis defined in the successor).
 *
 * For phis in the successor, we require sources and destination files to match
 * (ensuring correct pressure calculations). Insert spills/reloads accordingly.
 */
static ATTRIBUTE_NOINLINE void
reload_preds(struct spill_ctx *ctx, struct u_sparse_bitset *W, jay_block *succ)
{
   jay_foreach_predecessor(succ, pred, ctx->file) {
      U_SPARSE_BITSET_FOREACH_SET(W, v) {
         if (u_sparse_bitset_test(&succ->live_in, v)) {
            if (!u_sparse_bitset_test(&ctx->blocks[(*pred)->index].W_out, v)) {
               insert_reload(ctx, jay_edge_to_block(*pred, succ, ctx->file),
                             jay_along_edge(*pred, succ, ctx->file), v);
            }
         }
      }

      jay_foreach_phi_src_in_block(*pred, src) {
         assert(jay_phi_src_index(src) < ctx->n);
         if (src->src[0].file == ctx->file) {
            unsigned src_idx = jay_index(src->src[0]);
            assert(src_idx < ctx->n);

            if (!u_sparse_bitset_test(W, jay_phi_src_index(src))) {
               ensure_spilled(ctx, src_idx);
               assert(!BITSET_TEST(ctx->remat, src_idx));

               /* Use the spilled version */
               src->src[0] = jay_def_spilled(ctx, src_idx);
               jay_set_phi_src_index(src, ctx->n + jay_phi_src_index(src));
            } else if (!u_sparse_bitset_test(&ctx->blocks[(*pred)->index].W_out,
                                             src_idx)) {
               /* Fill the phi source in the predecessor */
               insert_reload(ctx, jay_edge_to_block(*pred, succ, ctx->file),
                             jay_along_edge(*pred, succ, ctx->file), src_idx);
            }

            remap_inst(ctx, ctx->blocks[(*pred)->index].remap, src);
         }
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
         if (I->src[s].file == ctx->file) {
            if (I->op != JAY_OPCODE_PHI_SRC) {
               util_dynarray_append(&ctx->next_ip, lookup_next_use(ctx, v));
            }

            ctx->next_uses[v] = ip;
            u_sparse_bitset_set(&ctx->N, v);
         }
      }

      if (I->dst.file == ctx->file) {
         jay_foreach_index_rev(I->dst, _, v) {
            if (v < ctx->n) {
               util_dynarray_append(&ctx->next_ip, lookup_next_use(ctx, v));
            } else {
               assert(I->op == JAY_OPCODE_PHI_DST);
            }
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
       * W, we implicitly chose which phis are spilled. So, here we just need to
       * rewrite the phis to write into memory.
       *
       * Phi sources are handled later.
       */
      if (I->op == JAY_OPCODE_PHI_DST) {
         if (jay_index(I->dst) < ctx->n) {
            if (I->dst.file == ctx->file) {
               if (!u_sparse_bitset_test(&ctx->W, jay_index(I->dst))) {
                  ctx->spill_block[jay_index(I->dst)] = NULL;
                  I->dst = jay_def_spilled(ctx, jay_index(I->dst));
               }
            }

            ctx->ip += inst_cycles(I);
         }
         continue;
      } else if (I->op == JAY_OPCODE_PHI_SRC) {
         break;
      }

      /* Any source that is not in W needs to be reloaded. Gather the set R of
       * such values, and add them to the register file.
       */
      unsigned R[JAY_MAX_SRCS * JAY_MAX_DEF_LENGTH], nR = 0;

      jay_foreach_src_index(I, s, c, v) {
         if (I->src[s].file == ctx->file && !u_sparse_bitset_test(&ctx->W, v)) {
            assert(nR < ARRAY_SIZE(R) && "maximum source count");
            R[nR++] = v;
            insert_W(ctx, v);
         }
      }

      /* Limit W to make space for the operands. */
      limit(ctx, I,
            ctx->k - (I->dst.file == ctx->file ? jay_num_values(I->dst) : 0));

      /* Add destinations to the register file */
      if (I->dst.file == ctx->file) {
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
         if (I->src[s].file == ctx->file) {
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

      remap_inst(ctx, ctx->blocks[block->index].remap, I);
      ctx->ip += inst_cycles(I);

      if (jay_debug & JAY_DBG_PRINTDEMAND) {
         printf("(SP) %u: ", ctx->nW);
         jay_print_inst(stdout, I);
      }
   }

   assert(next_use_cursor == 0 && "exactly sized");
   u_sparse_bitset_dup(&sb->W_out, &ctx->W);
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
      if (I->dst.file == ctx->file) {
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

   /* Variables that are in all predecessors are assumed in W_entry. Phis are
    * scored by next-use. Unlike the paper, variables in only some predecessors
    * are dropped, implementing an ACO-style "lazy reloading" which appears to
    * have advantages for divergent control flow.
    */
   U_SPARSE_BITSET_FOREACH_SET(&ctx->N, i) {
      bool all = true;

      jay_foreach_predecessor(block, P, ctx->file) {
         all &= u_sparse_bitset_test(&ctx->blocks[(*P)->index].W_out, i);

         /* When spilling UGPRs, consider only values that have never been
          * spilled nor remapped and therefore will not insert phi instructions.
          * That ensures correctness despite critical edges in the physical CFG.
          */
         all &= ctx->file == GPR ||
                !_mesa_hash_table_u64_search(ctx->blocks[(*P)->index].remap, i);
      }

      if (all) {
         insert_W(ctx, i);
      }
   }

   jay_foreach_phi_dst_in_block(block, I) {
      if (I->dst.file == ctx->file) {
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
            if (I->src[s].file == ctx->file &&
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
      jay_foreach_predecessor(block, pred, ctx->file) {
         if (minimum_next_uses(&ctx->blocks[(*pred)->index].next_use_out,
                               &sb->next_use_in, ctx->next_uses, &ctx->N)) {
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

static void
add_phi(struct spill_ctx *ctx,
        jay_block *succ,
        uint32_t node,
        struct hash_table_u64 *out_remap)
{
   int32_t remapped = -1;
   bool trivial = true;

   jay_foreach_predecessor(succ, pred, ctx->file) {
      int32_t v = lookup_remap(ctx->blocks[(*pred)->index].remap, node);
      trivial &= remapped < 0 || remapped == v || (v == 0 && succ->loop_header);
      remapped = MAX2(remapped, v);
   }

   if (!trivial) {
      /* If the value differs across predecessors, insert a phi for it. */
      jay_builder b = jay_init_builder(ctx->func, jay_before_block(succ));
      jay_def def = jay_alloc_def(&b, ctx->file, 1);
      jay_PHI_DST(&b, def);
      remapped = jay_index(def);

      jay_foreach_predecessor(succ, pred, ctx->file) {
         b.cursor = jay_after_block_logical(*pred);

         uint32_t src = lookup_remap(ctx->blocks[(*pred)->index].remap, node);
         jay_PHI_SRC_u32(&b, jay_scalar(ctx->file, src ? src : node), remapped);
      }
   }

   if (remapped) {
      _mesa_hash_table_u64_insert(out_remap, node,
                                  (void *) (uintptr_t) remapped);
   }
}

/*
 * UGPRs spill to GPRs so this (pre-RA) lowering is much simpler: just lower MOV
 * to SHUFFLE to legalize. Most of the time no actual shuffles are needed so
 * we're lazy initializing active_lane_x4. The initialization is required
 * per-block since we need an active lane.
 */
static void
lower_ugpr_spill(jay_function *func)
{
   jay_foreach_block(func, block) {
      jay_def active_lane_x4 = jay_null();

      jay_foreach_inst_in_block_safe(block, I) {
         if (I->op == JAY_OPCODE_MOV &&
             I->dst.file == UGPR &&
             I->src[0].file == GPR) {

            jay_builder b = jay_init_builder(func, jay_before_block(block));
            if (jay_is_null(active_lane_x4)) {
               jay_def ballot = jay_alloc_def(&b, FLAG, 1);
               jay_def lane = jay_alloc_def(&b, UGPR, 1);

               jay_ZERO_FLAG(&b, 0);
               jay_inst *mov = jay_MOV(&b, jay_null(), 1);
               jay_set_conditional_mod(&b, mov, ballot, GEN_CONDITION_NE);
               jay_FBL(&b, lane, ballot);

               active_lane_x4 = jay_SHL_u32(&b, lane, 2);
            }

            b.cursor = jay_before_inst(I);
            jay_SHUFFLE(&b, I->dst, I->src[0], active_lane_x4);
            jay_remove_instruction(I);
         }
      }
   }
}

void
jay_spill(jay_function *func, enum jay_file file, unsigned k)
{
   /* lower_ugpr_spill needs a UGPR temporary */
   k -= (file == UGPR) ? 1 : 0;

   void *memctx = ralloc_context(NULL);
   void *linctx = linear_context(memctx);
   struct spill_ctx ctx = { .func = func, .k = k };

   ctx.n = func->ssa_alloc;
   ctx.file = file;
   ctx.in_file = BITSET_LINEAR_ZALLOC(linctx, ctx.n);
   ctx.remat = BITSET_LINEAR_ZALLOC(linctx, ctx.n);
   ctx.defs = linear_zalloc_array(linctx, jay_inst *, ctx.n);
   ctx.spill_block = linear_zalloc_array(linctx, jay_block *, ctx.n);
   ctx.next_uses = linear_alloc_array(linctx, dist_t, ctx.n);
   ctx.candidates = linear_alloc_array(linctx, struct next_use, ctx.n);
   ctx.blocks =
      linear_zalloc_array(linctx, struct spill_block, func->num_blocks);

   jay_foreach_inst_in_func(func, block, I) {
      jay_foreach_dst_index(I, dst, idx) {
         ctx.defs[idx] = I;
         ctx.spill_block[idx] = block;

         if (can_remat(I)) {
            BITSET_SET(ctx.remat, idx);
         }
      }

      if (I->dst.file == file) {
         BITSET_SET_COUNT(ctx.in_file, jay_base_index(I->dst),
                          jay_num_values(I->dst));
      }
   }

   /* Don't remat phi sources since it ends up worse in practice */
   jay_foreach_block(func, block) {
      jay_foreach_phi_src_in_block(block, phi) {
         BITSET_CLEAR(ctx.remat, jay_index(phi->src[0]));
      }

      ctx.blocks[block->index].remap = _mesa_hash_table_u64_create(memctx);
   }

   u_sparse_bitset_init(&ctx.W, ctx.n, memctx);
   u_sparse_bitset_init(&ctx.N, ctx.n, memctx);
   util_dynarray_init(&ctx.next_ip, memctx);

   global_next_use_distances(&ctx, memctx);

   /* Reserve a memory variable for every regular variable */
   func->ssa_alloc *= 2;

   jay_foreach_block(func, block) {
      ctx.nW = 0;
      ctx.ip = 0;

      u_sparse_bitset_clear_all(&ctx.W);
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
         if (I->dst.file == file && jay_index(I->dst) < ctx.n) {
            assert(nu_cursor >= 1);
            ctx.next_uses[jay_index(I->dst)] = next_ips[--nu_cursor];
            u_sparse_bitset_set(&ctx.N, jay_index(I->dst));
         }
      }

      if (block->loop_header) {
         compute_w_entry_loop_header(&ctx, block);
         u_sparse_bitset_dup(&sb->W_in, &ctx.W);
      } else if (jay_num_predecessors(block, file) /* skip start blocks */) {
         compute_w_entry(&ctx, block);
         reload_preds(&ctx, &ctx.W, block);

         U_SPARSE_BITSET_FOREACH_SET(&ctx.W, idx) {
            add_phi(&ctx, block, idx, ctx.blocks[block->index].remap);
         }
      }

      assert(ctx.nW <= ctx.k && "invariant");
      min_algorithm(&ctx, block, sb, next_ips, nu_cursor);

      /* Handle loop back edges */
      struct jay_block *loop_head = block->logical_succs[0];

      if ((loop_head && loop_head->loop_header) &&
          loop_head->index < block->index) {
         reload_preds(&ctx, &ctx.blocks[loop_head->index].W_in, loop_head);

         struct hash_table_u64 *remap = _mesa_hash_table_u64_create(memctx);
         U_SPARSE_BITSET_FOREACH_SET(&ctx.blocks[loop_head->index].W_in, idx) {
            if (u_sparse_bitset_test(&loop_head->live_in, idx)) {
               add_phi(&ctx, loop_head, idx, remap);
            }
         }

         jay_foreach_block_from(func, loop_head, inside) {
            bool is_break_block = true;
            jay_foreach_successor(inside, succ, file) {
               is_break_block &= succ->index > block->index;
            }

            /* Remap to use our phis inside the loop */
            jay_foreach_inst_in_block(inside, I) {
               if (!(I->op == JAY_OPCODE_PHI_SRC && is_break_block)) {
                  remap_inst(&ctx, remap, I);
               }
            }

            /* Propagate outside the loop as necessary */
            hash_table_u64_foreach(remap, ent) {
               struct hash_table_u64 *ht = ctx.blocks[inside->index].remap;

               if (!_mesa_hash_table_u64_search(ht, ent.key)) {
                  _mesa_hash_table_u64_insert(ht, ent.key, ent.data);
               }
            }

            if (inside == block) {
               break;
            }
         }
      }
   }

   ralloc_free(memctx);

   if (file == UGPR) {
      lower_ugpr_spill(func);
   }

   /* We've inserted invalid dead phis, clean them up. */
   jay_opt_dead_code(func->shader);
   jay_validate(func->shader, "spill");
}
