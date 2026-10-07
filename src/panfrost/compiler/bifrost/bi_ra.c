/*
 * Copyright (C) 2020,2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_memory.h"
#include "bi_debug.h"
#include "bi_builder.h"
#include "compiler.h"
#include "nodearray.h"
#include "valhall.h"

struct lcra_state {
   unsigned node_count;

   /* Affinity used by the solver: one of aff[] below. The interference
    * graph is the same for the 32 register (split file) and 64 register
    * attempts, so it is built once with both affinity masks. */
   uint64_t *affinity;
   uint64_t *aff[2];

   /* Registers forced before solving, to reset solutions between attempts */
   unsigned *forced;

   /* Linear constraints imposed. For each node there there is a
    * 'nodearray' structure, which changes between a sparse and dense
    * array depending on the number of elements.
    *
    * Each element is itself a bit field denoting whether (c_j - c_i) bias
    * is present or not, including negative biases.
    *
    * We support up to 8 components so the bias is in range
    * [-7, 7] encoded by a 16-bit field
    */
   nodearray *linear;

   /* Before solving, forced registers; after solving, solutions. */
   unsigned *solutions;

   /** Every node that failed, when solving past failures */
   unsigned *failed;
   unsigned failed_count;

   /* While building from scratch, constraints are queued here as
    * (row << 40 | key << 16 | value) and sorted into rows at the end, which
    * is much cheaper than inserting each into a sorted sparse row. */
   struct util_dynarray *pending;
};

/* This module is an implementation of "Linearly Constrained
 * Register Allocation". The paper is available in PDF form
 * (https://people.collabora.com/~alyssa/LCRA.pdf) as well as Markdown+LaTeX
 * (https://gitlab.freedesktop.org/alyssa/lcra/blob/master/LCRA.md)
 */

static struct lcra_state *
lcra_alloc_equations(unsigned node_count)
{
   struct lcra_state *l = calloc(1, sizeof(*l));

   l->node_count = node_count;

   l->linear = calloc(sizeof(l->linear[0]), node_count);
   l->solutions = calloc(sizeof(l->solutions[0]), node_count);
   l->forced = calloc(sizeof(l->forced[0]), node_count);
   l->aff[0] = calloc(sizeof(l->aff[0][0]), node_count);
   l->aff[1] = calloc(sizeof(l->aff[1][0]), node_count);

   memset(l->solutions, ~0, sizeof(l->solutions[0]) * node_count);

   return l;
}

static void
lcra_free(struct lcra_state *l)
{
   for (unsigned i = 0; i < l->node_count; ++i)
      nodearray_reset(&l->linear[i]);

   free(l->linear);
   free(l->aff[0]);
   free(l->aff[1]);
   free(l->forced);
   free(l->solutions);
   free(l->failed);
   free(l);
}

static void lcra_flush_pending(struct lcra_state *l);

static void
lcra_add_node_interference(struct lcra_state *l, unsigned i, unsigned cmask_i,
                           unsigned j, unsigned cmask_j)
{
   if (i == j)
      return;

   nodearray_value constraint_fw = 0;
   nodearray_value constraint_bw = 0;

   /* The constraint bits are reversed from lcra.c so that register
    * allocation can be done in parallel for every possible solution,
    * with lower-order bits representing smaller registers. */

   if (cmask_i == 1 && cmask_j == 1) {
      constraint_fw = constraint_bw = 1 << 7;
   } else for (unsigned D = 0; D < 8; ++D) {
      if (cmask_i & (cmask_j << D)) {
         constraint_fw |= (1 << (7 + D));
         constraint_bw |= (1 << (7 - D));
      }

      if (cmask_i & (cmask_j >> D)) {
         constraint_bw |= (1 << (7 + D));
         constraint_fw |= (1 << (7 - D));
      }
   }

   if (l->pending) {
      assert(i < (1 << 24) && j < (1 << 24));
      if (constraint_fw)
         util_dynarray_append_typed(l->pending, uint64_t,
                                    ((uint64_t)j << 40) | ((uint64_t)i << 16) |
                                       constraint_fw);
      if (constraint_bw)
         util_dynarray_append_typed(l->pending, uint64_t,
                                    ((uint64_t)i << 40) | ((uint64_t)j << 16) |
                                       constraint_bw);

      /* Bound the queue's memory on huge shaders */
      if (util_dynarray_num_elements(l->pending, uint64_t) >= (1 << 20))
         lcra_flush_pending(l);
      return;
   }

   /* Keep rows sparse until they hold a quarter of the nodes, where a dense
    * row gets smaller. The solver scans a dense row in full for every use,
    * which dominated solving big shaders with a 256 element limit. */
   nodearray_orr(&l->linear[j], i, constraint_fw, ~0u, l->node_count);
   nodearray_orr(&l->linear[i], j, constraint_bw, ~0u, l->node_count);
}

/* Sorts the queued constraints into the rows: counting sort by key, then
 * stably by row, so each row's new keys come out sorted and merge with the
 * row in one pass. Rows get the representation nodearray_orr would have
 * given them.
 */
static void
lcra_flush_pending(struct lcra_state *l)
{
   unsigned n = l->node_count;
   unsigned count = util_dynarray_num_elements(l->pending, uint64_t);
   uint64_t *a = l->pending->data;
   /* Also scratch for merging a row: sparse rows hold under n / 4 keys */
   uint64_t *b = malloc(sizeof(uint64_t) * (count + n / 4 + 1));
   unsigned *pos = malloc(sizeof(unsigned) * (n + 1));

   memset(pos, 0, sizeof(unsigned) * (n + 1));
   for (unsigned e = 0; e < count; ++e)
      pos[((a[e] >> 16) & 0xffffff) + 1]++;
   for (unsigned k = 0; k < n; ++k)
      pos[k + 1] += pos[k];
   for (unsigned e = 0; e < count; ++e)
      b[pos[(a[e] >> 16) & 0xffffff]++] = a[e];

   memset(pos, 0, sizeof(unsigned) * (n + 1));
   for (unsigned e = 0; e < count; ++e)
      pos[(b[e] >> 40) + 1]++;
   for (unsigned k = 0; k < n; ++k)
      pos[k + 1] += pos[k];
   for (unsigned e = 0; e < count; ++e)
      a[pos[b[e] >> 40]++] = b[e];

   /* pos[r] is now the end of row r */
   for (unsigned r = 0, start = 0; r < n; start = pos[r++]) {
      if (start == pos[r])
         continue;

      nodearray *row = &l->linear[r];

      if (!nodearray_is_sparse(row)) {
         for (unsigned e = start; e < pos[r]; ++e)
            row->dense[(a[e] >> 16) & 0xffffff] |= a[e] & 0xffff;
         continue;
      }

      /* Merge the row with its new keys, ORing equal keys */
      unsigned u = 0, o = 0;
      unsigned e = start;

      while (o < row->size || e < pos[r]) {
         nodearray_sparse x;

         if (e == pos[r] ||
             (o < row->size && nodearray_sparse_key(&row->sparse[o]) <=
                                  ((a[e] >> 16) & 0xffffff)))
            x = row->sparse[o++];
         else
            x = a[e++] & BITFIELD64_MASK(40);

         if (u && nodearray_sparse_key(&b[u - 1]) == nodearray_sparse_key(&x))
            b[u - 1] |= x;
         else
            b[u++] = x;
      }

      nodearray_reset(row);

      if (u < n / 4) {
         row->sparse = malloc(sizeof(nodearray_sparse) * u);
         memcpy(row->sparse, b, sizeof(nodearray_sparse) * u);
         row->size = row->sparse_capacity = u;
      } else {
         row->dense =
            calloc(NODEARRAY_DENSE_ALIGN(n), sizeof(nodearray_value));
         row->size = n;
         row->sparse_capacity = ~0U;

         for (unsigned k = 0; k < u; ++k)
            row->dense[nodearray_sparse_key(&b[k])] =
               nodearray_sparse_value(&b[k]);
      }
   }

   free(pos);
   free(b);
   util_dynarray_clear(l->pending);
}

static bool
lcra_test_linear(struct lcra_state *l, unsigned *solutions, unsigned i)
{
   signed constant = solutions[i];

   if (nodearray_is_sparse(&l->linear[i])) {
      nodearray_sparse_foreach(&l->linear[i], elem) {
         unsigned j = nodearray_sparse_key(elem);
         nodearray_value constraint = nodearray_sparse_value(elem);

         if (solutions[j] == ~0)
            continue;

         signed lhs = constant - solutions[j];

         if (lhs < -7 || lhs > 7)
            continue;

         if (constraint & (1 << (lhs + 7)))
            return false;
      }

      return true;
   }

   nodearray_value *row = l->linear[i].dense;

   for (unsigned j = 0; j < l->node_count; ++j) {
      if (solutions[j] == ~0)
         continue;

      signed lhs = constant - solutions[j];

      if (lhs < -7 || lhs > 7)
         continue;

      if (row[j] & (1 << (lhs + 7)))
         return false;
   }

   return true;
}

/* Registers node i cannot take given the nodes solved so far. Bit (d + 7) of
 * a constraint forbids c_i - c_j == d, i.e. c_i == c_j + d. One pass over the
 * row gives the answer for all 64 registers, where lcra_test_linear needs a
 * pass per candidate register.
 */
static uint64_t
lcra_forbidden(struct lcra_state *l, unsigned i)
{
   uint64_t forbidden = 0;

#define LCRA_FORBID(j, constraint)                                             \
   do {                                                                        \
      if (l->solutions[j] != ~0 && (constraint)) {                             \
         int sh = (int)l->solutions[j] - 7;                                    \
         uint64_t m = (uint64_t)(constraint);                                  \
         forbidden |= sh >= 0 ? (m << sh) : (m >> -sh);                        \
      }                                                                        \
   } while (0)

   if (nodearray_is_sparse(&l->linear[i])) {
      nodearray_sparse_foreach(&l->linear[i], elem)
         LCRA_FORBID(nodearray_sparse_key(elem), nodearray_sparse_value(elem));
   } else {
      nodearray_value *row = l->linear[i].dense;

      for (unsigned j = 0; j < l->node_count; ++j)
         LCRA_FORBID(j, row[j]);
   }

#undef LCRA_FORBID
   return forbidden;
}

/* With keep_going, a node that fails is left unassigned (it constrains nothing)
 * and solving continues, so one pass finds every node that does not fit. The
 * spiller then handles all of them at once instead of one per full RA pass.
 */
static bool
lcra_solve(struct lcra_state *l, bool keep_going)
{
   l->failed_count = 0;
   if (keep_going && !l->failed)
      l->failed = malloc(sizeof(*l->failed) * l->node_count);

   for (unsigned step = 0; step < l->node_count; ++step) {
      if (l->solutions[step] != ~0)
         continue;
      if (l->affinity[step] == 0)
         continue;

      bool succ = false;

      /* First fit: the lowest allowed register */
      uint64_t allowed = l->affinity[step] & ~lcra_forbidden(l, step);
      if (allowed) {
         l->solutions[step] = ffsll(allowed) - 1;
         succ = true;
      }

      /* Out of registers - prepare to spill */
      if (!succ) {
         if (!keep_going)
            return false;

         l->failed[l->failed_count++] = step;
         l->solutions[step] = ~0;
      }
   }

   return l->failed_count == 0;
}

/* Register spilling is implemented with a cost-benefit system. Costs are set
 * by the user. Benefits are calculated from the constraints. */

static unsigned
lcra_count_constraints(struct lcra_state *l, unsigned i)
{
   unsigned count = 0;
   nodearray *constraints = &l->linear[i];

   if (nodearray_is_sparse(constraints)) {
      nodearray_sparse_foreach(constraints, elem)
         count += util_bitcount(nodearray_sparse_value(elem));
   } else {
      nodearray_dense_foreach_64(constraints, elem)
         count += util_bitcount64(*elem);
   }

   return count;
}

/* Liveness analysis is a backwards-may dataflow analysis pass. Within a block,
 * we compute live_out from live_in. The intrablock pass is linear-time. It
 * returns whether progress was made. */

static void
bi_liveness_ins_update_ra(uint8_t *live, bi_instr *ins)
{
   /* live_in[s] = GEN[s] + (live_out[s] - KILL[s]) */

   bi_foreach_dest(ins, d) {
      live[ins->dest[d].value] &= ~bi_writemask(ins, d);
   }

   bi_foreach_ssa_src(ins, src) {
      unsigned count = bi_count_read_registers(ins, src);
      unsigned rmask = BITFIELD_MASK(count);

      live[ins->src[src].value] |= (rmask << ins->src[src].offset);
   }
}

/* Only nodes that some block reads before writing them (upward-exposed) can
 * be live at a block boundary, so block liveness tracks just those "global"
 * nodes. That makes liveness O(blocks * globals) instead of
 * O(blocks * nodes), which matters for big shaders with many blocks.
 * block->live_in/live_out are indexed by global index.
 */
struct bi_ra_live {
   unsigned *gidx;  /* node -> global index, or ~0 */
   unsigned *glist; /* global index -> node */
   unsigned count;
};

static void
bi_find_global_nodes(bi_context *ctx, struct bi_ra_live *g)
{
   unsigned n = ctx->ssa_alloc;
   unsigned *stamp = calloc(n, sizeof(unsigned));
   uint8_t *written = malloc(n);

   g->gidx = malloc(n * sizeof(unsigned));
   g->glist = malloc(n * sizeof(unsigned));
   g->count = 0;
   memset(g->gidx, ~0, n * sizeof(unsigned));

   unsigned s = 0;
   bi_foreach_block(ctx, blk) {
      ++s;
      bi_foreach_instr_in_block(blk, ins) {
         bi_foreach_ssa_src(ins, src) {
            unsigned v = ins->src[src].value;
            unsigned rmask = BITFIELD_MASK(bi_count_read_registers(ins, src))
                             << ins->src[src].offset;
            uint8_t w = stamp[v] == s ? written[v] : 0;

            if ((rmask & ~w) && g->gidx[v] == ~0u) {
               g->gidx[v] = g->count;
               g->glist[g->count++] = v;
            }
         }

         bi_foreach_dest(ins, d) {
            unsigned v = ins->dest[d].value;

            if (stamp[v] != s) {
               stamp[v] = s;
               written[v] = 0;
            }
            written[v] |= bi_writemask(ins, d);
         }
      }
   }

   free(stamp);
   free(written);
}

static void
bi_ra_live_fini(struct bi_ra_live *g)
{
   free(g->gidx);
   free(g->glist);
}

static bool
liveness_block_update(bi_block *blk, const struct bi_ra_live *g)
{
   unsigned temp_count = g->count;
   bool progress = false;

   /* live_out[s] = sum { p in succ[s] } ( live_in[p] ) */
   bi_foreach_successor(blk, succ) {
      for (unsigned i = 0; i < temp_count; ++i)
         blk->live_out[i] |= succ->live_in[i];
   }

   uint8_t *live = ralloc_array(blk, uint8_t, MAX2(temp_count, 1));
   memcpy(live, blk->live_out, temp_count);

   /* bi_liveness_ins_update_ra over global nodes only */
   bi_foreach_instr_in_block_rev(blk, ins) {
      bi_foreach_dest(ins, d) {
         unsigned k = g->gidx[ins->dest[d].value];
         if (k != ~0u)
            live[k] &= ~bi_writemask(ins, d);
      }

      bi_foreach_ssa_src(ins, src) {
         unsigned k = g->gidx[ins->src[src].value];
         if (k != ~0u) {
            unsigned rmask = BITFIELD_MASK(bi_count_read_registers(ins, src));
            live[k] |= (rmask << ins->src[src].offset);
         }
      }
   }

   /* To figure out progress, diff live_in */

   for (unsigned i = 0; (i < temp_count) && !progress; ++i)
      progress |= (blk->live_in[i] != live[i]);

   ralloc_free(blk->live_in);
   blk->live_in = live;

   return progress;
}

/* Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */

static void
bi_compute_liveness_ra(bi_context *ctx, struct bi_ra_live *g)
{
   bi_find_global_nodes(ctx, g);

   u_worklist worklist;
   bi_worklist_init(ctx, &worklist);

   bi_foreach_block(ctx, block) {
      ralloc_free(block->live_in);
      ralloc_free(block->live_out);

      block->live_in = rzalloc_array(block, uint8_t, MAX2(g->count, 1));
      block->live_out = rzalloc_array(block, uint8_t, MAX2(g->count, 1));

      bi_worklist_push_tail(&worklist, block);
   }

   while (!u_worklist_is_empty(&worklist)) {
      /* Pop off in reverse order since liveness is backwards */
      bi_block *blk = bi_worklist_pop_tail(&worklist);

      /* Update liveness information. If we made progress, we need to
       * reprocess the predecessors
       */
      if (liveness_block_update(blk, g)) {
         bi_foreach_predecessor(blk, pred)
            bi_worklist_push_head(&worklist, *pred);
      }
   }

   u_worklist_fini(&worklist);
}

/* Construct an affinity mask such that the vector with `count` elements does
 * not intersect any of the registers in the bitset `clobber`. In other words,
 * an allocated register r needs to satisfy for each i < count: a + i != b.
 * Equivalently that's a != b - i, so we need a \ne { b - i : i < n }. For the
 * entire clobber set B, we need a \ne union b \in B { b - i : i < n }, where
 * that union is the desired clobber set. That may be written equivalently as
 * the union over i < n of (B - i), where subtraction is defined elementwise
 * and corresponds to a shift of the entire bitset.
 *
 * EVEN_BITS_MASK is an affinity mask for aligned register pairs. Interpreted
 * as a bit set, it is { x : 0 <= x < 64 if x is even }
 */

#define EVEN_BITS_MASK (0x5555555555555555ull)

static uint64_t
bi_make_affinity(uint64_t clobber, unsigned count, bool split_file)
{
   uint64_t clobbered = 0;

   for (unsigned i = 0; i < count; ++i)
      clobbered |= (clobber >> i);

   /* Don't allocate past the end of the register file */
   if (count > 1) {
      unsigned excess = count - 1;
      uint64_t mask = BITFIELD_MASK(excess);
      clobbered |= mask << (64 - excess);

      if (split_file)
         clobbered |= mask << (16 - excess);
   }

   /* Don't allocate the middle if we split out the middle */
   if (split_file)
      clobbered |= BITFIELD64_MASK(32) << 16;

   /* We can use a register iff it's not clobberred */
   return ~clobbered;
}

/* The nodes with a nonzero live mask, so interference only walks live nodes
 * instead of every node for every destination.
 */
struct bi_live_set {
   unsigned *list, *pos, count;
};

static inline void
bi_live_set_add(struct bi_live_set *s, unsigned v)
{
   if (s->pos[v] == ~0u) {
      s->pos[v] = s->count;
      s->list[s->count++] = v;
   }
}

static inline void
bi_live_set_del(struct bi_live_set *s, unsigned v)
{
   if (s->pos[v] != ~0u) {
      unsigned last = s->list[--s->count];
      s->list[s->pos[v]] = last;
      s->pos[last] = s->pos[v];
      s->pos[v] = ~0u;
   }
}

/* With dirty == NULL, records every constraint. Otherwise only constraints
 * involving a dirty node, and only dirty nodes' affinities: dls then tracks
 * the live dirty nodes, so clean definitions only walk those.
 */
static void
bi_mark_interference(bi_block *block, struct lcra_state *l, uint8_t *live,
                     struct bi_live_set *ls, struct bi_live_set *dls,
                     const BITSET_WORD *dirty, uint64_t preload_live,
                     bool is_blend, unsigned arch)
{
   bool aligned_sr = arch >= 9;

#define IS_DIRTY(v) (!dirty || BITSET_TEST(dirty, (v)))

   bi_foreach_instr_in_block_rev(block, ins) {
      /* Mark all registers live after the instruction as
       * interfering with the destination */

      bi_foreach_dest(ins, d) {
         unsigned node = ins->dest[d].value;
         bool node_dirty = IS_DIRTY(node);

         /* Don't allocate to anything that's read later as a
          * preloaded register. The affinity is the intersection
          * of affinity masks for each write. Since writes have
          * offsets, but the affinity is for the whole node, we
          * need to offset the affinity opposite the write
          * offset, so we shift right. */
         unsigned count = bi_count_write_registers(ins, d);
         unsigned offset = ins->dest[d].offset;
         for (unsigned f = 0; f < 2 && node_dirty; ++f) {
            uint64_t affinity =
               bi_make_affinity(preload_live, count, f == 0) >> offset;
            /* Valhall needs >= 64-bit staging writes to be pair-aligned */
            if (aligned_sr && (count >= 2 || offset))
               affinity &= EVEN_BITS_MASK;

            l->aff[f][node] &= affinity;
         }

         struct bi_live_set *s = node_dirty ? ls : dls;

         for (unsigned k = 0; k < s->count; ++k) {
            unsigned i = s->list[k];
            uint8_t r = live[i];

            /* Nodes only interfere if they occupy
             * /different values/ at the same time
             * (Boissinot). In particular, sources of
             * moves do not interfere with their
             * destinations. This enables a limited form of
             * coalescing.
             */
            if (ins->op == BI_OPCODE_MOV_I32 && bi_is_ssa(ins->src[0]) &&
                i == ins->src[0].value) {

               r &= ~BITFIELD_BIT(ins->src[0].offset);
            }

            if (r) {
               lcra_add_node_interference(l, node, bi_writemask(ins, d), i, r);
            }
         }

         unsigned node_first = ins->dest[0].value;
         if (d == 1 && (node_dirty || IS_DIRTY(node_first))) {
            lcra_add_node_interference(l, node, bi_writemask(ins, 1),
                                       node_first, bi_writemask(ins, 0));
         }
      }

      /* MMUL must not write its result into a multiply operand, but the
       * accumulator (src2) may be reused.
       */
      if (ins->op == BI_OPCODE_MMUL_F32 ||
          ins->op == BI_OPCODE_MMUL_V2F16 ||
          ins->op == BI_OPCODE_MMUL_V4S8 ||
          ins->op == BI_OPCODE_MMUL_V4U8) {
         const unsigned dnode = ins->dest[0].value;
         const unsigned dmask = bi_writemask(ins, 0);

         for (unsigned s = 0; s < 2; ++s) {
            if (bi_is_ssa(ins->src[s]) &&
                (IS_DIRTY(dnode) || IS_DIRTY(ins->src[s].value)))
               lcra_add_node_interference(l, dnode, dmask, ins->src[s].value,
                                          dmask);
         }
      }

      /* Valhall needs >= 64-bit reads to be pair-aligned */
      if (aligned_sr) {
         bi_foreach_ssa_src(ins, s) {
            if (!IS_DIRTY(ins->src[s].value))
               continue;

            if (bi_count_read_registers(ins, s) >= 2 ||
                (s < valhall_opcodes[ins->op].nr_srcs &&
                 va_src_info(ins->op, s).size > VA_SIZE_32)) {
               l->aff[0][ins->src[s].value] &= EVEN_BITS_MASK;
               l->aff[1][ins->src[s].value] &= EVEN_BITS_MASK;
            }
         }
      }

      if (!is_blend && ins->op == BI_OPCODE_BLEND) {
         /* Blend shaders might clobber r0-r15, blend link reg. */
         uint64_t clobber =
            BITFIELD64_MASK(16) |
            BITFIELD64_BIT(bi_preload_reg(BI_PRELOAD_BLEND_LINK, arch));
         struct bi_live_set *s = dirty ? dls : ls;

         for (unsigned k = 0; k < s->count; ++k) {
            l->aff[0][s->list[k]] &= ~clobber;
            l->aff[1][s->list[k]] &= ~clobber;
         }
      }

      /* Update live_in */
      preload_live = bi_postra_liveness_ins(preload_live, ins);

      /* bi_liveness_ins_update_ra, keeping the live sets in sync */
      bi_foreach_dest(ins, d) {
         unsigned v = ins->dest[d].value;
         live[v] &= ~bi_writemask(ins, d);
         if (!live[v]) {
            bi_live_set_del(ls, v);
            if (dirty && BITSET_TEST(dirty, v))
               bi_live_set_del(dls, v);
         }
      }

      bi_foreach_ssa_src(ins, src) {
         unsigned v = ins->src[src].value;
         unsigned rmask = BITFIELD_MASK(bi_count_read_registers(ins, src));
         live[v] |= (rmask << ins->src[src].offset);
         if (live[v]) {
            bi_live_set_add(ls, v);
            if (dirty && BITSET_TEST(dirty, v))
               bi_live_set_add(dls, v);
         }
      }
   }

#undef IS_DIRTY

   block->reg_live_in = preload_live;
}

static void
bi_compute_interference(bi_context *ctx, struct lcra_state *l,
                        const BITSET_WORD *dirty)
{
   struct bi_ra_live g;
   bi_compute_liveness_ra(ctx, &g);
   bi_postra_liveness(ctx);

   unsigned n = ctx->ssa_alloc;
   uint8_t *live = calloc(n, 1);
   struct bi_live_set ls = {
      .list = malloc(sizeof(unsigned) * n),
      .pos = malloc(sizeof(unsigned) * n),
   };
   struct bi_live_set dls = {
      .list = malloc(sizeof(unsigned) * n),
      .pos = malloc(sizeof(unsigned) * n),
   };
   memset(ls.pos, ~0, sizeof(unsigned) * n);
   memset(dls.pos, ~0, sizeof(unsigned) * n);

   /* live[] and the live sets are all clear between blocks: only the global
    * nodes live out of the block need to be set up. */
   bi_foreach_block_rev(ctx, blk) {
      for (unsigned k = 0; k < g.count; ++k) {
         if (blk->live_out[k]) {
            unsigned v = g.glist[k];
            live[v] = blk->live_out[k];
            bi_live_set_add(&ls, v);
            if (dirty && BITSET_TEST(dirty, v))
               bi_live_set_add(&dls, v);
         }
      }

      bi_mark_interference(blk, l, live, &ls, &dls, dirty, blk->reg_live_out,
                           ctx->inputs->is_blend, ctx->arch);

      for (unsigned k = 0; k < ls.count; ++k) {
         live[ls.list[k]] = 0;
         ls.pos[ls.list[k]] = ~0u;
      }
      for (unsigned k = 0; k < dls.count; ++k)
         dls.pos[dls.list[k]] = ~0u;
      ls.count = dls.count = 0;
   }

   free(live);
   free(ls.list);
   free(ls.pos);
   free(dls.list);
   free(dls.pos);
   bi_ra_live_fini(&g);
}

/* Default affinities (of dirty nodes, or all with dirty == NULL) and forced
 * registers */
static void
bi_lcra_init_nodes(bi_context *ctx, struct lcra_state *l,
                   const BITSET_WORD *dirty)
{
   /* Blend shaders are restricted to R0-R15. Other shaders at full
    * occupancy also can access R48-R63. At half occupancy they can access
    * the whole file. */
   uint64_t default_affinity[2];

   for (unsigned f = 0; f < 2; ++f) {
      bool full_regs = f == 1;
      default_affinity[f] =
         ctx->inputs->is_blend ? BITFIELD64_MASK(16)
         : full_regs           ? BITFIELD64_MASK(64)
                     : (BITFIELD64_MASK(16) | (BITFIELD64_MASK(16) << 48));

      /* To test spilling, mimic a small register file */
      if (bifrost_debug & BIFROST_DBG_SPILL && !ctx->inputs->is_blend && (bifrost_debug & BIFROST_DBG_NOSSARA))
         default_affinity[f] &= BITFIELD64_MASK(48) << 8;
   }

   memset(l->solutions, ~0, sizeof(l->solutions[0]) * l->node_count);

   bi_foreach_instr_global(ctx, ins) {
      bi_foreach_dest(ins, d) {
         unsigned v = ins->dest[d].value;
         if (!dirty || BITSET_TEST(dirty, v)) {
            l->aff[0][v] = default_affinity[0];
            l->aff[1][v] = default_affinity[1];
         }
      }

      /* Blend shaders expect the src colour to be in blend_src0_c0
       * through c3 */
      if (ins->op == BI_OPCODE_BLEND && !ctx->inputs->is_blend) {
         assert(bi_is_ssa(ins->src[0]));
         l->solutions[ins->src[0].value] =
            bi_preload_reg(BI_PRELOAD_BLEND_SRC0_C0, ctx->arch);

         /* Dual source blend input in blend_src1_c0 through c3 */
         if (bi_is_ssa(ins->src[4]))
            l->solutions[ins->src[4].value] =
               bi_preload_reg(BI_PRELOAD_BLEND_SRC1_C0, ctx->arch);

         /* Writes to blend link */
         if (!bi_is_null(ins->dest[0]))
            l->solutions[ins->dest[0].value] =
               bi_preload_reg(BI_PRELOAD_BLEND_LINK, ctx->arch);
      }

      /* Coverage mask writes stay in the cumulative coverage reg */
      if ((ins->op == BI_OPCODE_ATEST || ins->op == BI_OPCODE_ZS_EMIT) &&
          !bi_is_null(ins->dest[0])) {
         l->solutions[ins->dest[0].value] =
            bi_preload_reg(BI_PRELOAD_CUMULATIVE_COVERAGE, ctx->arch);
      }

      /* Experimentally, it seems coverage masks inputs to ATEST must
       * be in the cumulative coverage reg. Otherwise coverage mask
       * writes do not work with early-ZS with pixel-frequency-shading
       * (this combination of settings is legal if depth/stencil
       * writes are disabled). Allowing a FAU index also seems to
       * work on Valhall, at least.
       */
      if (ins->op == BI_OPCODE_ATEST) {
         assert(bi_is_ssa(ins->src[0]) || ins->src[0].type == BI_INDEX_FAU);
         if (bi_is_ssa(ins->src[0]))
            l->solutions[ins->src[0].value] =
               bi_preload_reg(BI_PRELOAD_CUMULATIVE_COVERAGE, ctx->arch);
      }
   }

   memcpy(l->forced, l->solutions, sizeof(l->solutions[0]) * l->node_count);
}

/* Builds the constraints, shared by the 32 and 64 register attempts */
static struct lcra_state *
bi_build_lcra(bi_context *ctx)
{
   struct lcra_state *l = lcra_alloc_equations(ctx->ssa_alloc);

   struct util_dynarray pending;
   util_dynarray_init(&pending, NULL);
   l->pending = &pending;

   bi_lcra_init_nodes(ctx, l, NULL);
   bi_compute_interference(ctx, l, NULL);
   lcra_flush_pending(l);

   l->pending = NULL;
   util_dynarray_fini(&pending);
   return l;
}

static void
nodearray_clear_key(nodearray *a, unsigned key)
{
   if (!nodearray_is_sparse(a)) {
      if (key < a->size)
         a->dense[key] = 0;
      return;
   }

   if (!a->size)
      return;

   nodearray_sparse *elem;
   unsigned idx = nodearray_sparse_search(a, key, &elem);

   if (nodearray_sparse_key(elem) == key) {
      memmove(elem, elem + 1, (a->size - idx - 1) * sizeof(*elem));
      a->size--;
   }
}

static void
lcra_grow(struct lcra_state *l, unsigned n)
{
   unsigned old = l->node_count;

   if (n == old)
      return;

   l->linear = realloc(l->linear, n * sizeof(l->linear[0]));
   memset(l->linear + old, 0, (n - old) * sizeof(l->linear[0]));

   for (unsigned i = 0; i < old; ++i) {
      nodearray *a = &l->linear[i];

      if (!nodearray_is_sparse(a)) {
         unsigned from = NODEARRAY_DENSE_ALIGN(a->size);
         unsigned to = NODEARRAY_DENSE_ALIGN(n);

         a->dense = realloc(a->dense, to * sizeof(nodearray_value));
         memset(a->dense + from, 0, (to - from) * sizeof(nodearray_value));
         a->size = n;
      }
   }

#define GROW(arr)                                                              \
   do {                                                                        \
      arr = realloc(arr, n * sizeof(arr[0]));                                  \
      memset(arr + old, 0, (n - old) * sizeof(arr[0]));                        \
   } while (0)

   GROW(l->aff[0]);
   GROW(l->aff[1]);
   GROW(l->solutions);
   GROW(l->forced);
#undef GROW

   free(l->failed);
   l->failed = NULL;
   l->node_count = n;
}

/* Spilling only changes the spilled nodes and adds fill temporaries: no
 * other node gains or loses a definition, and the spill code reads and
 * writes nothing else, so every other node's liveness is unchanged. Remove
 * the spilled nodes' constraints and recompute those involving a spilled
 * node or a new node. The result is the same as a full rebuild, without
 * redoing the constraints between all the other nodes.
 */
static void
bi_update_lcra(bi_context *ctx, struct lcra_state *l, const unsigned *spilled,
               unsigned spilled_count)
{
   unsigned old = l->node_count, n = ctx->ssa_alloc;

   lcra_grow(l, n);

   BITSET_WORD *dirty = BITSET_CALLOC(n);
   BITSET_SET_RANGE(dirty, old, n - 1);

   for (unsigned s = 0; s < spilled_count; ++s) {
      unsigned x = spilled[s];
      nodearray *row = &l->linear[x];

      BITSET_SET(dirty, x);

      if (nodearray_is_sparse(row)) {
         nodearray_sparse_foreach(row, elem)
            nodearray_clear_key(&l->linear[nodearray_sparse_key(elem)], x);
      } else {
         for (unsigned j = 0; j < row->size; ++j) {
            if (row->dense[j])
               nodearray_clear_key(&l->linear[j], x);
         }
      }

      nodearray_reset(row);

      /* init_nodes only resets the affinity of nodes it finds defined */
      l->aff[0][x] = l->aff[1][x] = 0;
   }

   bi_lcra_init_nodes(ctx, l, dirty);
   bi_compute_interference(ctx, l, dirty);
   free(dirty);
}

static bool
bi_solve_lcra(bi_context *ctx, struct lcra_state *l, bool full_regs)
{
   l->affinity = l->aff[full_regs];
   memcpy(l->solutions, l->forced, sizeof(l->solutions[0]) * l->node_count);

   /* Coalesce register moves if we're allowed. We need to be careful due
    * to the restricted affinity induced by the blend shader ABI.
    */
   bi_foreach_instr_global(ctx, I) {
      if (I->op != BI_OPCODE_MOV_I32)
         continue;
      if (I->src[0].type != BI_INDEX_REGISTER)
         continue;

      unsigned reg = I->src[0].value;
      unsigned node = I->dest[0].value;

      if (l->solutions[node] != ~0)
         continue;

      uint64_t affinity = l->affinity[node];

      if (ctx->inputs->is_blend) {
         /* We're allowed to coalesce the moves to these */
         affinity |=
            BITFIELD64_BIT(bi_preload_reg(BI_PRELOAD_BLEND_LINK, ctx->arch));
         affinity |= BITFIELD64_BIT(
            bi_preload_reg(BI_PRELOAD_CUMULATIVE_COVERAGE, ctx->arch));
      }

      /* Try to coalesce */
      if (affinity & BITFIELD64_BIT(reg)) {
         l->solutions[node] = reg;

         if (!lcra_test_linear(l, l->solutions, node))
            l->solutions[node] = ~0;
      }
   }

   /* Only the full register file attempt spills, so only it needs every
    * failing node. */
   return lcra_solve(l, full_regs);
}

static bi_index
bi_reg_from_index(bi_context *ctx, struct lcra_state *l, bi_index index)
{
   /* Offsets can only be applied when we register allocated an index, or
    * alternatively for FAU's encoding */

   ASSERTED bool is_offset = (index.offset > 0) && (index.type != BI_INDEX_FAU);

   /* Did we run RA for this index at all */
   if (!bi_is_ssa(index)) {
      assert(!is_offset);
      return index;
   }

   /* LCRA didn't bother solving this index (how lazy!) */
   signed solution = l->solutions[index.value];
   if (solution < 0) {
      assert(0 && "no solution for index");
      return index;
   }

   /* todo: do we want to compose with the subword swizzle? */
   bi_index new_index = bi_register(solution + index.offset);
   new_index.swizzle = index.swizzle;
   new_index.abs = index.abs;
   new_index.neg = index.neg;
   return new_index;
}

/* Dual texture instructions write to two sets of staging registers, modeled as
 * two destinations in the IR. The first set is communicated with the usual
 * staging register mechanism. The second set is encoded in the texture
 * operation descriptor. This is quite unusual, and requires the following late
 * fixup.
 */
static void
bi_fixup_dual_tex_register(bi_instr *I)
{
   assert(I->dest[1].type == BI_INDEX_REGISTER);
   assert(I->src[3].type == BI_INDEX_CONSTANT);

   struct bifrost_dual_texture_operation desc = {
      .secondary_register = I->dest[1].value,
   };

   I->src[3].value |= bi_dual_tex_as_u32(desc);
}

static void
bi_install_registers(bi_context *ctx, struct lcra_state *l)
{
   bi_foreach_instr_global(ctx, ins) {
      bi_foreach_dest(ins, d)
         ins->dest[d] = bi_reg_from_index(ctx, l, ins->dest[d]);

      bi_foreach_src(ins, s)
         ins->src[s] = bi_reg_from_index(ctx, l, ins->src[s]);

      if (ins->op == BI_OPCODE_TEXC_DUAL)
         bi_fixup_dual_tex_register(ins);
   }
}

static void
bi_rewrite_index_src_single(bi_instr *ins, bi_index old, bi_index new)
{
   bi_foreach_src(ins, i) {
      if (bi_is_equiv(ins->src[i], old)) {
         ins->src[i].type = new.type;
         ins->src[i].value = new.value;
      }
   }
}

/* Nodes that must not be spilled, satisfying bi_spill_register's
 * preconditions */

static BITSET_WORD *
bi_no_spill_set(bi_context *ctx, struct lcra_state *l)
{
   BITSET_WORD *no_spill = BITSET_CALLOC(l->node_count);

   bi_foreach_instr_global(ctx, ins) {
      bi_foreach_dest(ins, d) {
         /* Don't allow spilling coverage mask writes because the
          * register preload logic assumes it will stay in the
          * cumulative coverage reg. This could be optimized.
          */
         if (ins->no_spill || ins->op == BI_OPCODE_ATEST ||
             ins->op == BI_OPCODE_ZS_EMIT ||
             (ins->op == BI_OPCODE_MOV_I32 &&
              ins->src[0].type == BI_INDEX_REGISTER &&
              ins->src[0].value ==
                 bi_preload_reg(BI_PRELOAD_CUMULATIVE_COVERAGE, ctx->arch))) {
            BITSET_SET(no_spill, ins->dest[d].value);
         }
      }
   }

   return no_spill;
}

static unsigned
bi_spill_benefit(struct lcra_state *l, unsigned *benefit, unsigned i)
{
   if (benefit[i] == ~0)
      benefit[i] = lcra_count_constraints(l, i);

   return benefit[i];
}

/* If register allocation fails at `node`, find the best spill node */

static signed
bi_choose_spill_node(struct lcra_state *l, BITSET_WORD *no_spill,
                     unsigned *benefit, unsigned node)
{
   unsigned best_benefit = 0;
   signed best_node = -1;

   /* consider spilling the node that we're trying to allocate,
    * rather the ones interfering with it, if that would be
    * better
    */
   if (!BITSET_TEST(no_spill, node)) {
      best_node = node;
      best_benefit = bi_spill_benefit(l, benefit, node);
   }

   if (nodearray_is_sparse(&l->linear[node])) {
      nodearray_sparse_foreach(&l->linear[node], elem) {
         unsigned i = nodearray_sparse_key(elem);
         unsigned constraint = nodearray_sparse_value(elem);

         /* Only spill nodes that interfere with the node failing
          * register allocation. It's pointless to spill anything else */
         if (!constraint)
            continue;

         if (BITSET_TEST(no_spill, i))
            continue;

         unsigned b = bi_spill_benefit(l, benefit, i);

         if (b > best_benefit) {
            best_benefit = b;
            best_node = i;
         }
      }
   } else {
      nodearray_value *row = l->linear[node].dense;

      for (unsigned i = 0; i < l->node_count; ++i) {
         /* Only spill nodes that interfere with the node failing
          * register allocation. It's pointless to spill anything else */
         if (!row[i])
            continue;

         if (BITSET_TEST(no_spill, i))
            continue;

         unsigned b = bi_spill_benefit(l, benefit, i);

         if (b > best_benefit) {
            best_benefit = b;
            best_node = i;
         }
      }
   }

   return best_node;
}

static unsigned
bi_count_read_index(bi_instr *I, bi_index index)
{
   unsigned max = 0;

   bi_foreach_src(I, s) {
      if (bi_is_equiv(I->src[s], index)) {
         unsigned count = bi_count_read_registers(I, s);
         max = MAX2(max, count + I->src[s].offset);
      }
   }

   return max;
}

/*
 * Wrappers to emit loads/stores to thread-local storage in an appropriate way
 * for the target, so the spill/fill code becomes architecture-independent.
 */

static bi_index
bi_tls_ptr(bool hi)
{
   return bi_fau(BIR_FAU_TLS_PTR, hi);
}

bi_instr *
bi_load_tl(bi_builder *b, unsigned bits, bi_index dst, unsigned offset)
{
   bi_instr *I;
   if (b->shader->arch >= 9) {
      assert(offset < 0x8000);  /* valhall has 16 bit signed offset */
      I = bi_load_to(b, bits, dst, bi_tls_ptr(false), bi_tls_ptr(true),
                     BI_SEG_TL, offset);
   } else {
      I = bi_load_to(b, bits, dst, bi_imm_u32(offset), bi_zero(), BI_SEG_TL, 0);
   }
   I->mem_access = VA_MEMORY_ACCESS_FORCE;

   return I;
}

void
bi_store_tl(bi_builder *b, unsigned bits, bi_index src, unsigned offset)
{
   bi_instr *I;
   if (b->shader->arch >= 9) {
      assert(offset < 0x8000);  /* valhall has 16 bit signed offset */
      I = bi_store(b, bits, src, bi_tls_ptr(false), bi_tls_ptr(true), BI_SEG_TL,
                   offset);
   } else {
      I = bi_store(b, bits, src, bi_imm_u32(offset), bi_zero(), BI_SEG_TL, 0);
   }
   I->mem_access = VA_MEMORY_ACCESS_FORCE;
}

static void
bi_compute_reg_alignment(bi_context *ctx)
{
   unsigned idx;
   unsigned count;
   ctx->reg_alignment = rzalloc_array(ctx, uint8_t, ctx->ssa_alloc);
   bi_foreach_instr_global(ctx, I) {
      bi_foreach_ssa_dest(I, d) {
         idx = I->dest[d].value;
         count = bi_count_write_registers(I, d) + I->dest[d].offset;
         if (count == 3) count = 4;
         assert(idx < ctx->ssa_alloc);
         ctx->reg_alignment[idx] = MAX2(count*4, ctx->reg_alignment[idx]);
      }
      bi_foreach_ssa_src(I, s) {
         idx = I->src[s].value;
         count = bi_count_read_index(I, I->src[s]);
         if (count == 3) count = 4;
         assert(idx < ctx->ssa_alloc);
         ctx->reg_alignment[idx] = MAX2(count*4, ctx->reg_alignment[idx]);
      }
   }
}

struct bi_spill_ref {
   bi_block *block;
   bi_instr *I;
};

static void
bi_spill_ref_add(struct util_dynarray *refs, bi_block *block, bi_instr *I)
{
   struct bi_spill_ref *last =
      refs->size ? util_dynarray_top_ptr(refs, struct bi_spill_ref) : NULL;
   if (!last || last->I != I)
      util_dynarray_append_typed(refs, struct bi_spill_ref,
                                 ((struct bi_spill_ref){block, I}));
}

/* Spills nodes[k] with spill point points[k] for each k in turn, returning the
 * new (aligned) offset. Spilling a node only inserts instructions referencing
 * that node and new temporaries, and only rewrites that node's sources, so
 * the instructions referencing each node (or defining its spill point) are
 * collected in one scan instead of scanning the shader once per node.
 */
static unsigned
bi_spill_registers(bi_context *ctx, const unsigned *nodes,
                   const unsigned *points, unsigned count, uint32_t offset)
{
   bi_builder b = {.shader = ctx};
   if (!ctx->reg_alignment)
      bi_compute_reg_alignment(ctx);

   unsigned n = ctx->ssa_alloc;
   int *by_node = malloc(sizeof(int) * n);
   int *by_point = malloc(sizeof(int) * n);
   int *next_point = malloc(sizeof(int) * MAX2(count, 1));
   struct util_dynarray *refs = calloc(MAX2(count, 1), sizeof(*refs));
   memset(by_node, ~0, sizeof(int) * n);
   memset(by_point, ~0, sizeof(int) * n);

   for (unsigned k = 0; k < count; ++k) {
      assert(by_node[nodes[k]] < 0);
      by_node[nodes[k]] = k;
      next_point[k] = by_point[points[k]];
      by_point[points[k]] = k;
      util_dynarray_init(&refs[k], NULL);
   }

   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block(block, I) {
         bi_foreach_dest(I, d) {
            if (I->dest[d].type != BI_INDEX_NORMAL)
               continue;

            unsigned v = I->dest[d].value;
            for (int k = by_point[v]; k >= 0; k = next_point[k])
               bi_spill_ref_add(&refs[k], block, I);
            if (by_node[v] >= 0)
               bi_spill_ref_add(&refs[by_node[v]], block, I);
         }

         bi_foreach_src(I, s) {
            if (I->src[s].type == BI_INDEX_NORMAL &&
                by_node[I->src[s].value] >= 0)
               bi_spill_ref_add(&refs[by_node[I->src[s].value]], block, I);
         }
      }
   }

   for (unsigned k = 0; k < count; ++k) {
      bi_index index = bi_get_index(nodes[k]);
      bi_index spill_point = bi_get_index(points[k]);
      unsigned channels = ctx->reg_alignment[index.value] / 4;
      assert(channels > 0);
      unsigned alignment =
         (ctx->arch >= 9) ? ctx->reg_alignment[index.value] : 4;

      /* first figure out the alignment we will need, based on the
       * maximum count we see
       */
      offset = ALIGN_POT(offset, alignment);

      /* Spill after every store, fill before every load */
      bi_block *block = NULL;
      bool fill = true;
      bool found_spill_point = false;

      util_dynarray_foreach(&refs[k], struct bi_spill_ref, ref) {
         bi_instr *I = ref->I;

         if (ref->block != block) {
            block = ref->block;
            fill = true;
            found_spill_point = false;
         }

         bi_foreach_dest(I, d) {
            if (bi_is_equiv(I->dest[d], spill_point)) {
               found_spill_point = true;
               fill = true;
            }
            if (!bi_is_equiv(I->dest[d], index))
               continue;

            I->no_spill = true;
            unsigned count = bi_count_write_registers(I, d);
            unsigned extra = I->dest[d].offset;
            bi_index src = index;
            src.offset = extra;

            b.cursor = bi_after_instr(I);
            bi_store_tl(&b, count * 32, src, offset + (extra * 4));

            /* Don't disable filling if spill_point is before index. */
            fill = found_spill_point;
         }

         if (bi_has_arg(I, index) && fill) {
            b.cursor = bi_before_instr(I);
            bi_index tmp = bi_temp(ctx);

            unsigned bits = bi_count_read_index(I, index) * 32;
            bi_rewrite_index_src_single(I, index, tmp);

            bi_instr *ld = bi_load_tl(&b, bits, tmp, offset);
            ld->no_spill = true;
         }
      }

      offset += channels * 4;
      util_dynarray_fini(&refs[k]);
   }

   free(refs);
   free(next_point);
   free(by_point);
   free(by_node);
   return offset;
}

/*
 * For transition, lower collects and splits before RA, rather than after RA.
 * LCRA knows how to deal with offsets (broken SSA), but not how to coalesce
 * these vector moves.
 */
static void
bi_lower_vector(bi_context *ctx, unsigned first_reg)
{
   bi_index *remap = calloc(ctx->ssa_alloc, sizeof(bi_index));

   bi_foreach_instr_global_safe(ctx, I) {
      bi_builder b = bi_init_builder(ctx, bi_after_instr(I));

      if (I->op == BI_OPCODE_SPLIT_I32) {
         bi_index src = I->src[0];
         assert(src.offset == 0);

         bi_foreach_dest(I, i) {
            src.offset = i;
            bi_mov_i32_to(&b, I->dest[i], src);

            if (I->dest[i].value < first_reg)
               remap[I->dest[i].value] = src;
         }

         bi_remove_instruction(I);
      } else if (I->op == BI_OPCODE_COLLECT_I32) {
         bi_index dest = I->dest[0];
         assert(dest.offset == 0);
         assert(((dest.value < first_reg) || I->nr_srcs == 1) &&
                "nir_lower_phis_to_scalar");

         bi_foreach_src(I, i) {
            if (bi_is_null(I->src[i]))
               continue;

            dest.offset = i;
            bi_mov_i32_to(&b, dest, I->src[i]);
         }

         bi_remove_instruction(I);
      }
   }

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_ssa_src(I, s) {
         if (I->src[s].value < first_reg && !bi_is_null(remap[I->src[s].value]))
            bi_replace_src(I, s, remap[I->src[s].value]);
      }
   }

   free(remap);

   /* After generating a pile of moves, clean up */
   struct bi_ra_live g;
   bi_compute_liveness_ra(ctx, &g);

   /* Dead code removal only shrinks liveness, so live-in stays within the
    * global nodes. live[] is all clear between blocks; nodes set in a block
    * are recorded in touched[] to clear them again. */
   uint8_t *live = calloc(ctx->ssa_alloc, 1);
   struct util_dynarray touched;
   util_dynarray_init(&touched, NULL);

   bi_foreach_block_rev(ctx, block) {
      bi_foreach_successor(block, succ) {
         for (unsigned k = 0; k < g.count; ++k) {
            if (succ->live_in[k]) {
               live[g.glist[k]] |= succ->live_in[k];
               util_dynarray_append_typed(&touched, unsigned, g.glist[k]);
            }
         }
      }

      bi_foreach_instr_in_block_safe_rev(block, ins) {
         bool all_null = true;

         bi_foreach_dest(ins, d) {
            if (live[ins->dest[d].value] & bi_writemask(ins, d))
               all_null = false;
         }

         if (all_null && !bi_side_effects(ins)) {
            bi_remove_instruction(ins);
         } else {
            bi_liveness_ins_update_ra(live, ins);
            bi_foreach_ssa_src(ins, src) {
               util_dynarray_append_typed(&touched, unsigned,
                                          ins->src[src].value);
            }
         }
      }

      uint8_t *live_in = ralloc_array(block, uint8_t, MAX2(g.count, 1));
      for (unsigned k = 0; k < g.count; ++k)
         live_in[k] = live[g.glist[k]];

      ralloc_free(block->live_in);
      block->live_in = live_in;

      util_dynarray_foreach(&touched, unsigned, v)
         live[*v] = 0;
      util_dynarray_clear(&touched);
   }

   util_dynarray_fini(&touched);
   free(live);
   bi_ra_live_fini(&g);
}

/*
 * Check if the instruction requires a "tied" operand. Such instructions MUST
 * allocate their source and destination to the same register. This is a
 * constraint on RA, and may require extra moves.
 *
 * In particular, this is the case for Bifrost instructions that both read and
 * write with the staging register mechanism.
 */
static bool
bi_is_tied(const bi_instr *I)
{
   return (I->op == BI_OPCODE_TEXC || I->op == BI_OPCODE_TEXC_DUAL ||
           I->op == BI_OPCODE_ATOM_RETURN_I32 || I->op == BI_OPCODE_AXCHG_I32 ||
           I->op == BI_OPCODE_ACMPXCHG_I32 || I->op == BI_OPCODE_AXCHG_I64 ||
           I->op == BI_OPCODE_ACMPXCHG_I64) &&
          !bi_is_null(I->src[0]);
}

/*
 * For transition, coalesce tied operands together, as LCRA knows how to handle
 * non-SSA operands but doesn't know about tied operands.
 *
 * This breaks the SSA form of the program, but that doesn't matter for LCRA.
 */
static void
bi_coalesce_tied(bi_context *ctx)
{
   if (ctx->reg_alignment) {
      ralloc_free(ctx->reg_alignment);
      ctx->reg_alignment = NULL;
   }
   bi_foreach_instr_global(ctx, I) {
      if (!bi_is_tied(I))
         continue;

      bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
      unsigned n = bi_count_read_registers(I, 0);

      bi_index dst = I->dest[0], src = I->src[0];
      assert(dst.offset == 0);
      for (unsigned i = 0; i < n; ++i) {
         bi_mov_i32_to(&b, dst, src);
         dst.offset++;
         src.offset++;
      }

      bi_replace_src(I, 0, I->dest[0]);
   }
}

static unsigned
find_or_allocate_temp(unsigned *map, unsigned value, unsigned *alloc)
{
   if (!map[value])
      map[value] = ++(*alloc);

   assert(map[value]);
   return map[value] - 1;
}

/* Reassigns numbering to get rid of gaps in the indices and to prioritize
 * smaller register classes */

static void
squeeze_index(bi_context *ctx)
{
   unsigned *map = rzalloc_array(ctx, unsigned, ctx->ssa_alloc);
   ctx->ssa_alloc = 0;

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_dest(I, d)
         I->dest[d].value =
            find_or_allocate_temp(map, I->dest[d].value, &ctx->ssa_alloc);

      bi_foreach_ssa_src(I, s)
         I->src[s].value =
            find_or_allocate_temp(map, I->src[s].value, &ctx->ssa_alloc);
   }

   ralloc_free(map);
}

static void
bi_mov_words_to(bi_builder *b, bi_index dst, bi_index src, unsigned words)
{
   assert(words >= 1 && words <= 4);

   for (unsigned i = 0; i < words; ++i) {
      bi_index word_dst = dst, word_src = src;
      word_dst.offset += i;
      word_src.offset += i;

      bi_mov_i32_to(b, word_dst, word_src);
   }
}

/*
 * Brainless out-of-SSA pass. The eventual goal is to go out-of-SSA after RA and
 * coalesce implicitly with biased colouring in a tree scan allocator. For now,
 * this should be good enough for LCRA.
 */
static unsigned
bi_out_of_ssa(bi_context *ctx)
{
   bi_index zero = bi_fau(BIR_FAU_IMMEDIATE | 0, false);
   unsigned first_reg = ctx->ssa_alloc;
   bool allow_propagate;

   /* Trivially lower phis */
   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block_safe(block, I) {
         if (I->op != BI_OPCODE_PHI)
            break;

         unsigned words = I->table ? I->table : 1;
         assert(words >= 1 && words <= 4);

         /* Assign a register for the phi */
         bi_index reg = bi_temp(ctx);
         assert(reg.value >= first_reg);

         /* Lower to a move in each predecessor. The destinations
          * cannot interfere so these can be sequentialized
          * in arbitrary order.
          */
         bi_foreach_predecessor(block, pred) {
            bi_builder b = bi_init_builder(ctx, bi_after_block_logical(*pred));
            unsigned i = bi_predecessor_index(block, *pred);

            assert(!I->src[i].abs);
            assert(!I->src[i].neg);
            assert(I->src[i].swizzle == BI_SWIZZLE_H01);

            if (I->src[i].memory)
               /* spilled register, need to un-spill */
               bi_load_tl(&b, words * 32, reg, I->src[i].value);
            else if (ctx->arch >= 9 && I->src[i].type == BI_INDEX_CONSTANT) {
               /* MOV of immediate needs lowering on Valhall */
               for (unsigned w = 0; w < words; ++w) {
                  bi_index word_reg = reg;
                  word_reg.offset += w;
                  bi_iadd_imm_i32_to(&b, word_reg, zero,
                                     w == 0 ? I->src[i].value : 0);
               }
            } else
               bi_mov_words_to(&b, reg, I->src[i], words);
         }

         /* Replace the phi with a move */
         allow_propagate = true;
         bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
         if (I->dest[0].memory) {
            /* dest was spilled to memory */
            bi_store_tl(&b, words * 32, reg, I->dest[0].value);
            allow_propagate = false;
         } else if (ctx->arch >= 9 && reg.type == BI_INDEX_CONSTANT)
            /* MOV of immediate needs lowering on Valhall */
            bi_iadd_imm_i32_to(&b, I->dest[0], zero, reg.value);
         else
            bi_mov_words_to(&b, I->dest[0], reg, words);
         bi_remove_instruction(I);

         /* Propagate that move within the block. The destination
          * is SSA and the source is not written in this block,
          * so this is legal. The move itself will be DCE'd if
          * possible in the next pass.
          */
         bi_foreach_instr_in_block_rev(block, prop) {
            if (prop->op == BI_OPCODE_PHI || !allow_propagate)
               break;

            bi_foreach_src(prop, s) {
               if (bi_is_equiv(prop->src[s], I->dest[0])) {
                  bi_replace_src(prop, s, reg);
               }
            }
         }
      }
   }

   /* Try to locally propagate the moves we created. We need to be extra
    * careful because we're not in SSA at this point, as such this
    * algorithm is quadratic. This will go away when we go out of SSA after
    * RA.
    */
   BITSET_WORD *used = BITSET_CALLOC(ctx->ssa_alloc);
   BITSET_WORD *multiple_uses = BITSET_CALLOC(ctx->ssa_alloc);

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_ssa_src(I, s) {
         if (BITSET_TEST(used, I->src[s].value))
            BITSET_SET(multiple_uses, I->src[s].value);
         else
            BITSET_SET(used, I->src[s].value);
      }
   }

   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block_safe_rev(block, mov) {
         /* Match "reg = ssa" */
         if (mov->op != BI_OPCODE_MOV_I32)
            continue;
         if (mov->dest[0].type != BI_INDEX_NORMAL)
            continue;
         if (mov->dest[0].value < first_reg)
            continue;
         if (!bi_is_ssa(mov->src[0]))
            continue;
         if (mov->src[0].value >= first_reg)
            continue;
         if (BITSET_TEST(multiple_uses, mov->src[0].value))
            continue;

         bool found = false;

         /* Look locally for the write of the SSA */
         bi_foreach_instr_in_block_rev(block, I) {
            bool bail = false;

            bi_foreach_src(I, s) {
               /* Bail: write-after-read */
               if (bi_is_equiv(I->src[s], mov->dest[0]))
                  bail = true;
            }

            if (bail)
               break;

            bi_foreach_dest(I, d) {
               /* Bail: write-after-write */
               if (bi_is_equiv(I->dest[d], mov->dest[0]))
                  break;

               if (!bi_is_equiv(I->dest[d], mov->src[0]))
                  continue;

               /* We found it, replace */
               I->dest[d] = bi_replace_index(I->dest[d], mov->dest[0]);
               found = true;
               break;
            }

            if (found)
               break;
         }

         if (found)
            bi_remove_instruction(mov);
      }
   }

   free(used);
   free(multiple_uses);
   return first_reg;
}

static bool
op_is_load(enum bi_opcode op)
{
   switch (op) {
   case BI_OPCODE_LOAD_I8:
   case BI_OPCODE_LOAD_I16:
   case BI_OPCODE_LOAD_I24:
   case BI_OPCODE_LOAD_I32:
   case BI_OPCODE_LOAD_I48:
   case BI_OPCODE_LOAD_I64:
   case BI_OPCODE_LOAD_I96:
   case BI_OPCODE_LOAD_I128:
      return true;
   default:
      return false;
   }
}

static bool
op_is_store(enum bi_opcode op)
{
   switch (op) {
   case BI_OPCODE_STORE_I8:
   case BI_OPCODE_STORE_I16:
   case BI_OPCODE_STORE_I24:
   case BI_OPCODE_STORE_I32:
   case BI_OPCODE_STORE_I48:
   case BI_OPCODE_STORE_I64:
   case BI_OPCODE_STORE_I96:
   case BI_OPCODE_STORE_I128:
      return true;
   default:
      return false;
   }
}

static void
compute_spill_cost(bi_context *ctx)
{
   void *mctx = ralloc_context(NULL);

   /* Required for finding blocks belonging to loops. */
   bi_calc_dominance(ctx);

   /* The cost of a spill/fill is just 10*block_depth for now. */

   uint32_t *block_depth = rzalloc_array(mctx, uint32_t, ctx->num_blocks);
   BITSET_WORD *loop_block = BITSET_RZALLOC(mctx, ctx->num_blocks);

   bi_foreach_block(ctx, block) {
      if (!block->loop_header)
         continue;

      bi_find_loop_blocks(ctx, block, loop_block);

      for (uint32_t b = 0; b < ctx->num_blocks; ++b) {
         if (BITSET_TEST(loop_block, b))
            block_depth[b] += 1;
      }
   }

   unsigned spills = 0, fills = 0;
   uint64_t cost = 0;
   bi_foreach_block(ctx, block) {
      uint64_t per_spill_cost = 10 * (block_depth[block->index] + 1);
      bi_foreach_instr_in_block(block, I) {
         if (op_is_load(I->op) && I->seg == BI_SEG_TL) {
            fills++;
            cost += per_spill_cost;
         } else if (op_is_store(I->op) && I->seg == BI_SEG_TL) {
            spills++;
            cost += per_spill_cost;
         }
      }
   }

   ctx->spills = spills;
   ctx->fills = fills;
   ctx->spill_cost = cost;

   ralloc_free(mctx);
}

void
bi_register_allocate(bi_context *ctx)
{
   struct lcra_state *l = NULL;
   bool success = false;

   unsigned iter_count = 0;
   unsigned max_iters = 2000;
   /* Number of bytes of memory we've spilled into */
   unsigned spill_count = ctx->info.tls_size;

   if (ctx->arch >= 9)
      va_lower_split_64bit(ctx);

   /* get estimate of register demand (must be done in SSA form)
    * and do a preliminary spill; this doesn't have to be perfect,
    * since register allocation can spill too, but RA is really slow
    * so the closer we get to having enough registers free, the better
    */
   if (!(bifrost_debug & BIFROST_DBG_NOSSARA)) {
      unsigned regs_to_use =
         ((bifrost_debug & BIFROST_DBG_SPILL) && !ctx->inputs->is_blend) ? 16 : BI_MAX_REGS;
      bool verbose = bifrost_debug & BIFROST_DBG_VERBOSE;

      bi_compute_liveness_ssa(ctx);
      if (verbose) {
         bi_print_shader(ctx, stderr);
      }
      unsigned register_demand = bi_calc_register_demand(ctx);
      if (register_demand > regs_to_use) {
         /* spill registers if we can */
         if (ctx->inputs->is_blend)
            UNREACHABLE("Blend shaders may not spill");

         /* Spilling reserves 8 registers for its parallel copies on top of
          * the k it is given (see reserved_size() in bi_ra_ssa.c), so
          * k = 64 left ~87 registers of demand and LCRA had to spill the
          * rest one RA pass at a time. Leave room for the reservation.
          */
         bi_spill_ssa(ctx, MAX2(regs_to_use - 8, 16));
         spill_count += bi_lower_spill(ctx, spill_count);

         /* By default, we use packed TLS addressing on Valhall.
          * We cannot cross 16 byte boundaries with packed TLS
          * addressing. Align to ensure this doesn't happen. This
          * could be optimized a bit.
          */
         if (ctx->arch >= 9)
            spill_count = ALIGN_POT(spill_count, 16);
         if (verbose) {
            fprintf(stderr, "\nspill_registers=%d\n", spill_count);
            bi_print_shader(ctx, stderr);
         }
      }
   }

   /* Lower tied operands. SSA is broken from here on. */
   unsigned first_reg = bi_out_of_ssa(ctx);
   bi_lower_vector(ctx, first_reg);
   bi_coalesce_tied(ctx);
   squeeze_index(ctx);

   /* Try with reduced register pressure to improve thread count */
   l = bi_build_lcra(ctx);

   if (ctx->arch >= 7) {
      success = bi_solve_lcra(ctx, l, false);

      if (success)
         ctx->info.work_reg_count = 32;
   }

   /* Otherwise, use the register file and spill until we succeed */
   while (!success && ((iter_count++) < max_iters)) {
      success = bi_solve_lcra(ctx, l, true);

      if (success) {
         ctx->info.work_reg_count = BI_MAX_REGS;
      } else {
         if (ctx->inputs->is_blend)
            UNREACHABLE("Blend shaders may not spill");

         /* Spill one node for every node that failed. Each RA pass costs a
          * full liveness and interference rebuild, so spilling one node per
          * pass took hundreds of passes (seconds) on big shaders.
          */
         BITSET_WORD *no_spill = bi_no_spill_set(ctx, l);
         unsigned *benefit = malloc(sizeof(*benefit) * l->node_count);
         memset(benefit, ~0, sizeof(*benefit) * l->node_count);
         unsigned *spilled_nodes = malloc(sizeof(unsigned) * l->failed_count);
         unsigned *spill_points = malloc(sizeof(unsigned) * l->failed_count);
         unsigned spilled = 0;

         for (unsigned f = 0; f < l->failed_count; ++f) {
            signed spill_node =
               bi_choose_spill_node(l, no_spill, benefit, l->failed[f]);
            if (spill_node == -1)
               continue;

            /* Don't pick it again for another failed node */
            BITSET_SET(no_spill, spill_node);
            spill_points[spilled] = l->failed[f];
            spilled_nodes[spilled++] = spill_node;
         }

         spill_count = bi_spill_registers(ctx, spilled_nodes, spill_points,
                                          spilled, spill_count);
         free(spill_points);

         free(benefit);
         free(no_spill);

         if (spilled == 0)
            UNREACHABLE("Failed to choose spill node\n");

         /* In case the spill affected an instruction with tied
          * operands, we need to fix up.
          */
         bi_coalesce_tied(ctx);
         bi_update_lcra(ctx, l, spilled_nodes, spilled);
         free(spilled_nodes);
      }
   }

   compute_spill_cost(ctx);

   assert(success);
   assert(l != NULL);

   if (ctx->arch >= 9)
      spill_count = ALIGN_POT(spill_count, 16);
   ctx->info.tls_size = spill_count;
   bi_install_registers(ctx, l);

   lcra_free(l);
}
