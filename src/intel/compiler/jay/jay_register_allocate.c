/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include "util/bitset.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/sparse_bitset.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/**
 * Register allocation for Jay shaders.
 *
 * We use a decoupled register allocation approach.  First, we spill values
 * until the register demand fits within the size of each register file.
 *
 * Secondly, we assign registers using a tree-scan algorithm similar to the
 * one described in Colombet et al 2011:
 *
 *    Q. Colombet, B. Boissinot, P. Brisk, S. Hack and F. Rastello,
 *        "Graph-coloring and treescan register allocation using repairing,"
 *        2011 Proceedings of the 14th International Conference on Compilers,
 *        Architectures and Synthesis for Embedded Systems (CASES), Taipei,
 *        Taiwan, 2011, pp. 45-54, doi: 10.1145/2038698.2038708.
 *
 * We also use a union-find set to construct equivalence classes for phi webs,
 * and attempt to use the same regs for registers in that class, similar to
 * the "Aggressive Pre-Coalescing" step described in that paper.
 *
 * Finally, we deconstruct SSA.
 */

#define jay_foreach_ra_src(I, s)                                               \
   jay_foreach_src(I, s)                                                       \
      if (I->src[s].file < JAY_NUM_RA_FILES && !jay_is_null(I->src[s]))

struct affinity {
   /**
    * If there is a vector affinity defined for this SSA def, it is relative to
    * some representative SSA index. Else 0 if there is no affinity.
    */
   uint32_t repr;

   /** If the representative: offset in registers from the base.
    *
    * If not the representative: offset in registers from the representative. */
   signed offset:7;

   /**
    * If true, this value is used in an early end-of-thread SEND and requires
    * high registers.
    */
   bool eot:1;

   /**
    * If align is nonzero, this SSA def should be assigned to a register of the
    * form (k * align) + align_offs for some integer k. In other words, align is
    * the alignment of the whole vector and align_offs is this def's channel.
    */
   unsigned align     :7;
   unsigned align_offs:7;
   unsigned nr        :4;
   unsigned padding   :6;
};
static_assert(sizeof(struct affinity) == 8, "packed");

struct phi_web_node {
   /* Parent index, or circular for root */
   uint32_t parent;

   /* If root, assigned register, or ~0 if no register assigned. */
   uint16_t reg;

   /* Rank, at most log2(n) so need ~5-bits */
   uint16_t rank;

   /* If root, affinity for the whole web */
   struct affinity affinity;
};
static_assert(sizeof(struct phi_web_node) == 16, "packed");

static unsigned
phi_web_find(struct phi_web_node *web, unsigned x)
{
   if (web[x].parent == x) {
      /* Root */
      return x;
   } else {
      /* Search up the tree */
      unsigned root = x;
      while (web[root].parent != root)
         root = web[root].parent;

      /* Compress path. Second pass ensures O(1) memory usage. */
      while (web[x].parent != x) {
         unsigned temp = web[x].parent;
         web[x].parent = root;
         x = temp;
      }

      return root;
   }
}

static void
phi_web_union(struct phi_web_node *web, unsigned x, unsigned y)
{
   x = phi_web_find(web, x);
   y = phi_web_find(web, y);

   if (x == y)
      return;

   /* Union-by-rank: ensure x.rank >= y.rank */
   if (web[x].rank < web[y].rank) {
      SWAP(x, y);
   }

   web[y].parent = x;

   /* Increment rank if necessary */
   if (web[x].rank == web[y].rank) {
      web[x].rank++;
   }
}

#define NO_REG 0xFFFF

static inline jay_reg
make_reg(enum jay_file file, uint16_t reg)
{
   return (((uint16_t) file) << 13) | reg;
}

static inline unsigned
r_reg(jay_reg r)
{
   assert(r != NO_REG);
   return r & BITFIELD_MASK(13);
}

static inline enum jay_file
r_file(jay_reg r)
{
   assert(r != NO_REG);
   assert((r >> 13) < JAY_NUM_RA_FILES);
   return r >> 13;
}

static jay_def
def_from_reg(jay_reg r)
{
   return jay_bare_reg(r_file(r), r_reg(r));
}

struct jay_roundrobin {
   unsigned block, gpr;
};

typedef struct jay_ra_state {
   /** Size of each register file */
   unsigned num_regs[JAY_NUM_RA_FILES];

   /** Partition-aware counters for roundrobin register allocation */
   struct jay_roundrobin roundrobin[JAY_NUM_RA_FILES][JAY_NUM_STRIDES];

   /** Phi coalescing data structure */
   struct phi_web_node *phi_web;

   /**
    * Global SSA index -> jay_reg map. Unlike reg_for_index, once a register
    * is picked it will not be shuffled.
    */
   jay_reg *global_reg_for_index;

   /**
    * Block currently being processed. ra_state is allocated once per
    * function but the following fields are updated as we go through the
    * program. This keeps RA linearish time.
    */
   jay_block *block;

   /** Builder for inserting shuffle code */
   jay_builder b;

   /** Local SSA index -> jay_reg map. Only defined for live indices. */
   jay_reg *reg_for_index;

   /**
    * Value occupying a register (register -> uint32_t reverse maps) for
    * registers that are not available. Undefined for available registers.
    */
   uint32_t *index_for_reg[JAY_NUM_RA_FILES];

   /** Set of registers that are available */
   BITSET_WORD *available_regs[JAY_NUM_RA_FILES];

   /**
    * Within assign_regs_for_inst, the set of registers that are respectively
    * 1. assigned and therefore pinned; 2. the base of a killed source; 3. used
    * as sources not yet processed.
    *
    * Invariant: zeroed on entry to assign_regs_for_inst.
    */
   BITSET_WORD *pinned[JAY_NUM_RA_FILES], *killed[JAY_NUM_RA_FILES],
      *sources[JAY_NUM_RA_FILES];

   /** Vector affinities for each def. */
   struct affinity *affinities;
} jay_ra_state;

static bool
reg_is_available(const jay_ra_state *ra, jay_reg reg)
{
   assert(reg != NO_REG);
   return BITSET_TEST(ra->available_regs[r_file(reg)], r_reg(reg));
}

static inline jay_reg
current_reg(const jay_ra_state *ra, uint32_t index)
{
   assert(index > 0 && index < ra->b.func->ssa_alloc);
   jay_reg reg = ra->reg_for_index[index];

   assert(!reg_is_available(ra, reg));
   assert(ra->index_for_reg[r_file(reg)][r_reg(reg)] == index);
   return reg;
}

/** (dst, src) pairs for use in parallel copies */
struct jay_parallel_copy {
   jay_reg dst, src;
};

static void
add_copy(struct util_dynarray *copies, jay_reg dst, jay_reg src)
{
   if (dst != src) {
      assert(r_file(dst) == r_file(src));
      util_dynarray_append(copies, ((struct jay_parallel_copy) { dst, src }));
   }
}

static jay_def
push_temp(jay_builder *b,
          struct jay_temp_regs t,
          enum jay_file file,
          bool outer,
          jay_def *backing,
          jay_def avoid1,
          jay_def avoid2)
{
   assert(file == GPR || file == UGPR);
   jay_reg reg = file == GPR ? t.gpr : t.ugpr;
   jay_def tmp = reg == NO_REG ? jay_null() : def_from_reg(reg);

   if (!jay_is_null(tmp)) {
      return tmp;
   }

   /* Find a register that does not conflict with the inputs */
   bool avoid_regs[2] = { false, false };
   if (!jay_is_null(avoid1) && avoid1.file == file && avoid1.reg < 2) {
      avoid_regs[avoid1.reg] = true;
   }
   if (!jay_is_null(avoid2) && avoid2.file == file && avoid2.reg < 2) {
      avoid_regs[avoid2.reg] = true;
   }

   unsigned r = avoid_regs[0] ? (avoid_regs[1] ? 2 : 1) : 0;

   file = file == UGPR ? UACCUM : ACCUM;
   *backing = jay_bare_reg(file, outer * 2);

   /* Put accumulators down the float pipe - it's still a raw move. */
   jay_def new = def_from_reg(r);
   jay_MOV(b, *backing, new)->type = JAY_TYPE_F32;
   return new;
}

static void
pop_temp(jay_builder *b, jay_def temp, jay_def backing)
{
   if (!jay_is_null(backing)) {
      assert(backing.file == ACCUM || backing.file == UACCUM);
      jay_MOV(b, temp, backing)->type = JAY_TYPE_F32;
   }
}

/*
 * Insert a single logical copy. Like jay_MOV but expands to multiple moves
 * involving a temporary register in some cases.
 */
static void
mov(jay_builder *b, jay_def dst, jay_def src, struct jay_temp_regs temps)
{
   bool split_copy = dst.file == MEM && src.file == MEM;
   bool acc_src = false, acc_dst = false;

   if (dst.file == GPR && src.file == GPR) {
      struct jay_partition *p = &b->shader->partition;
      struct jay_register_block D = jay_lookup_block(p, dst.reg, GPR);
      struct jay_register_block S = jay_lookup_block(p, src.reg, GPR);

      acc_dst = D.type == JAY_BLOCK_ACCUM;
      acc_src = S.type == JAY_BLOCK_ACCUM;

      split_copy |= D.stride != S.stride &&
                    D.stride != JAY_STRIDE_4 &&
                    S.stride != JAY_STRIDE_4;

      split_copy |= (acc_dst && S.stride != JAY_STRIDE_4) ||
                    (acc_src && D.stride != JAY_STRIDE_4);
   }

   if (split_copy) {
      jay_def temp = jay_null(), backing = jay_null();
      temp = push_temp(b, temps, GPR, false, &backing, jay_null(), jay_null());
      jay_MOV(b, temp, src)->type = acc_src ? JAY_TYPE_F32 : JAY_TYPE_U32;
      jay_MOV(b, dst, temp)->type = acc_dst ? JAY_TYPE_F32 : JAY_TYPE_U32;
      pop_temp(b, temp, backing);
   } else {
      jay_MOV(b, dst, src)->type =
         (acc_src || acc_dst) ? JAY_TYPE_F32 : JAY_TYPE_U32;
   }
}

/*
 * Sequentialize a parallel copy. temps are registers free *before* the
 * parallel copy. A temporary might be the destination of a copy, but it
 * cannot be the source of any copy (since copying a free register is
 * undefined). Therefore it cannot be a part of a cycle, so it is free for use
 * (only) when handling cycles, which must happen before sequential copies.
 */
static void
jay_emit_parallel_copies(jay_builder *b,
                         struct jay_parallel_copy *pcopies,
                         unsigned num_copies,
                         struct jay_temp_regs temps)
{
   /* Compact away trivial copies upfront to reduce runtime. */
   unsigned new_num_copies = 0;
   for (unsigned i = 0; i < num_copies; ++i) {
      assert(r_file(pcopies[i].dst) == r_file(pcopies[i].src));

      if (pcopies[i].dst != pcopies[i].src) {
         pcopies[new_num_copies++] = pcopies[i];
      }
   }

   num_copies = new_num_copies;
   if (num_copies == 0)
      return;

   assert(num_copies < UINT16_MAX);
   BITSET_WORD *done = BITSET_CALLOC(num_copies);
   uint16_t *reg_use_count[JAY_NUM_RA_FILES];
   jay_foreach_ra_file(f) {
      reg_use_count[f] = calloc(b->shader->num_regs[f], sizeof(uint16_t));
   }

   struct jay_parallel_copy *simple = malloc(num_copies * sizeof(*simple));
   unsigned num_simple = 0;

#ifndef NDEBUG
   BITSET_WORD *packed = BITSET_CALLOC(UINT16_MAX);

   if (0) {
      printf("[[\n");

      for (unsigned i = 0; i < num_copies; i++) {
         printf("  %s%u = %s%u\n", jay_file_prefix(r_file(pcopies[i].dst)),
                r_reg(pcopies[i].dst), jay_file_prefix(r_file(pcopies[i].src)),
                r_reg(pcopies[i].src));
      }

      printf("]]\n");
   }

   /**
    * Assert that each parallel copy destination is unique: no reg can appear
    * as the destination of two parallel copies.
    */
   for (unsigned i = 0; i < num_copies; i++) {
      assert(!BITSET_TEST(packed, pcopies[i].dst));
      BITSET_SET(packed, pcopies[i].dst);
   }

   free(packed);
#endif

   for (unsigned i = 0; i < num_copies; i++) {
      ++reg_use_count[r_file(pcopies[i].src)][r_reg(pcopies[i].src)];
   }

   bool progress;
   do {
      progress = false;

      /* Step 1: resolve paths in the transfer graph. This means finding
       * copies whose destination aren't blocked by something else and then
       * emitting them, continuing this process until every copy is blocked
       * and there are only cycles left.
       *
       * TODO: We should note that src is also available in dest to unblock
       * cycles that src is involved in.
       */
      for (unsigned i = 0; i < num_copies; i++) {
         struct jay_parallel_copy *copy = &pcopies[i];

         if (!BITSET_TEST(done, i) &&
             reg_use_count[r_file(copy->dst)][r_reg(copy->dst)] == 0) {

            simple[num_simple++] = *copy;
            BITSET_SET(done, i);
            --reg_use_count[r_file(copy->src)][r_reg(copy->src)];
            progress = true;
         }
      }
   } while (progress);

   /* Step 2: resolve cycles through swapping.
    *
    * At this point, the transfer graph should consist of only cycles.
    * The reason is that, given any reg n_1 that's the source of a
    * remaining entry, it has a destination n_2, which (because every
    * copy is blocked) is the source of some other copy whose destination
    * is n_3, and so we can follow the chain until we get a cycle. If we
    * reached some other node than n_1:
    *
    * n_1 -> n_2 -> ... -> n_i
    *         ^             |
    *         |-------------|
    *
    * then n_2 would be the destination of 2 copies, which is illegal
    * (checked above in an assert). So n_1 must be part of a cycle:
    *
    * n_1 -> n_2 -> ... -> n_i
    * ^                     |
    * |---------------------|
    *
    * and this must be only cycle n_1 is involved in, because any other
    * path starting from n_1 would also have to end in n_1, resulting in
    * a node somewhere along the way being the destination of 2 copies
    * when the 2 paths merge.
    *
    * The way we resolve the cycle is through picking a copy (n_1, n_2)
    * and swapping n_1 and n_2. This moves n_1 to n_2, so n_2 is taken
    * out of the cycle:
    *
    * n_1 -> ... -> n_i
    * ^              |
    * |--------------|
    *
    * and we can keep repeating this until the cycle is empty. After each
    * swap, we update sources of blocking copies. At that point, every
    * blocking copy's source should be contained within our destination.
    */
   for (unsigned i = 0; i < num_copies; i++) {
      struct jay_parallel_copy *copy = &pcopies[i];

      if (!BITSET_TEST(done, i) && copy->dst != copy->src) {
         jay_def dst = def_from_reg(copy->dst), src = def_from_reg(copy->src);
         assert(dst.file == src.file);
         enum jay_file file = dst.file;

         if (file == GPR &&
             jay_def_stride(b->shader, dst) == JAY_STRIDE_4 &&
             jay_def_stride(b->shader, src) == JAY_STRIDE_4) {

            /* If everything is stride=4, swapping is easy */
            jay_def acc = jay_bare_reg(ACCUM, 2);
            jay_MOV(b, acc, dst)->type = JAY_TYPE_F32;
            jay_MOV(b, dst, src)->type = JAY_TYPE_F32;
            jay_MOV(b, src, acc)->type = JAY_TYPE_F32;
         } else {
            struct jay_temp_regs t = { .gpr = temps.gpr2, .ugpr = temps.ugpr };
            jay_def temp_backing = jay_null();
            jay_def temp =
               push_temp(b, temps, file == GPR || file == MEM ? GPR : UGPR,
                         true /* outer */, &temp_backing, dst, src);
            mov(b, temp, dst, t);
            mov(b, dst, src, t);
            mov(b, src, temp, t);
            pop_temp(b, temp, temp_backing);
         }

         for (unsigned j = 0; j < num_copies; j++) {
            if (pcopies[j].src == copy->dst)
               pcopies[j].src = copy->src;
         }

         /* Simple copies are deferred. Their destinations do not conflict with
          * our swaps, but we need to swap their sources to sink.
          */
         for (unsigned j = 0; j < num_simple; j++) {
            assert(simple[j].dst != copy->src && simple[j].dst != copy->dst);

            if (simple[j].src == copy->src)
               simple[j].src = copy->dst;
            else if (simple[j].src == copy->dst)
               simple[j].src = copy->src;
         }
      }

      BITSET_SET(done, i);
   }

   /* Emit moves after swaps because they fan out and thus increase demand.
    * This gives us more freedom around temporaries. The rewrite of simple
    * copies above ensures correctness.
    */
   for (unsigned i = 0; i < num_simple; i++) {
      jay_def dst = def_from_reg(simple[i].dst);
      jay_def src = def_from_reg(simple[i].src);

      mov(b, dst, src, temps);

      if (temps.gpr == simple[i].dst || temps.gpr == simple[i].src) {
         temps.gpr = NO_REG;
      }

      if (temps.ugpr == simple[i].dst || temps.ugpr == simple[i].src) {
         temps.ugpr = NO_REG;
      }
   }

   jay_foreach_ra_file(f) {
      free(reg_use_count[f]);
   }

   free(simple);
   free(done);
}

static void
assign_reg_for_index(jay_ra_state *ra, uint32_t index, jay_reg reg)
{
   /* Update our data structures */
   ra->reg_for_index[index] = reg;
   ra->index_for_reg[r_file(reg)][r_reg(reg)] = index;
   BITSET_CLEAR(ra->available_regs[r_file(reg)], r_reg(reg));

   /* Update the web to the most recent register. Heuristic from Colombet. */
   ra->phi_web[phi_web_find(ra->phi_web, index)].reg = reg;

   /* Post-conditions */
   assert(!reg_is_available(ra, reg));
   assert(current_reg(ra, index) == reg);
}

static void
release_reg(jay_ra_state *ra, jay_reg reg)
{
   /* Update available_regs only - the reg<-->index maps are invalidated. */
   BITSET_SET(ra->available_regs[r_file(reg)], r_reg(reg));
}

static unsigned
register_demand(jay_ra_state *ra, enum jay_file f)
{
   unsigned n = ra->num_regs[f];
   return n - __bitset_prefix_sum(ra->available_regs[f], n, BITSET_WORDS(n));
}

static bool
is_block_compatible(struct jay_register_block block,
                    enum jay_file file,
                    enum jay_stride min_stride,
                    enum jay_stride max_stride,
                    bool eot,
                    bool allow_accum)
{
   return block.type != JAY_BLOCK_SPILL &&
          (file != GPR ||
           (min_stride <= block.stride && block.stride <= max_stride)) &&
          (!eot || block.type == JAY_BLOCK_EOT) &&
          (allow_accum || block.type != JAY_BLOCK_ACCUM);
}

static jay_reg
try_find_free_reg(jay_ra_state *ra,
                  enum jay_file file,
                  unsigned except,
                  bool stride4)
{
   for (unsigned b = 0; b < ra->b.shader->partition.nr_blocks[file]; ++b) {
      struct jay_register_block B = ra->b.shader->partition.blocks[file][b];

      if (is_block_compatible(B, file, stride4 ? JAY_STRIDE_4 : 0,
                              stride4 ? JAY_STRIDE_4 : ~0, false, !stride4)) {

         for (unsigned i = B.start_gpr; i < B.start_gpr + B.len_gpr; ++i) {
            if (BITSET_TEST(ra->available_regs[file], i) && i != except) {
               return make_reg(file, i);
            }
         }
      }
   }

   return NO_REG;
}

static jay_reg
find_free_reg(jay_ra_state *ra, enum jay_file file, unsigned except)
{
   jay_reg reg = try_find_free_reg(ra, file, except, false);

   if (reg == NO_REG) {
      fprintf(stderr, "file %u, current demand %u, target %u\n", file,
              register_demand(ra, file), ra->num_regs[file]);
      UNREACHABLE("there should have been a free register");
   }

   return reg;
}

static inline struct jay_temp_regs
find_temp_regs(jay_ra_state *ra)
{
   /* For efficiency we only bother using stride=4 temporaries */
   jay_reg gpr = try_find_free_reg(ra, GPR, ~0, true);

   return (struct jay_temp_regs) {
      .gpr = gpr,
      .ugpr = try_find_free_reg(ra, UGPR, ~0, false),
      .gpr2 = try_find_free_reg(ra, GPR, gpr, true),
   };
}

static void
pick_regs_from_block(jay_ra_state *ra,
                     enum jay_file file,
                     unsigned size,
                     unsigned alignment,
                     jay_inst *I,
                     jay_def var,
                     bool is_src,
                     struct jay_register_block block,
                     unsigned block_cost,
                     struct affinity affinity,
                     unsigned *best_cost,
                     unsigned *best_reg,
                     unsigned first)
{
   /* Cross-lane access cannot be SIMD split if the source/destination registers
    * overlap, but as long as we don't tie those destinations, we're ok.
    */
   bool may_tie = !jay_is_shuffle_like(I);

   first = align(first, alignment);
   for (unsigned i = first; i + size <= block.len_gpr; i += alignment) {
      unsigned r = block.start_gpr + i;

      unsigned cost = block_cost;
      bool tied = !is_src && BITSET_TEST(ra->killed[file], r);
      if (tied ? !may_tie : BITSET_TEST_COUNT(ra->pinned[file], r, size))
         continue;

      /* Try to tie predicated default values, otherwise post-RA lowering needs
       * to insert a predicated-MOV or SEL.
       */
      if (I->predication == JAY_PREDICATED_DEFAULT && !is_src)
         cost += jay_inst_get_default(I)->reg != r;

      /* If there are stricter alignment requirements later, model the cost of
       * inserting copies for that.
       */
      if (affinity.align &&
          (i < affinity.align_offs ||
           !util_is_aligned(i - affinity.align_offs, affinity.align)))
         cost += size;

      if (affinity.repr == jay_channel(var, 0)) {
         /* If we are the collect representative but the final collect won't
          * actually be usable, the whole vector will need to be copied.
          */
         if (i < affinity.offset || !util_is_aligned(i - affinity.offset, 4)) {
            cost += affinity.nr;
         }
      } else if (affinity.repr) {
         /* If we are used for a collect but not in the right place, we will
          * similarly insert copies.
          */
         if (ra->reg_for_index[affinity.repr] != NO_REG &&
             r_reg(ra->reg_for_index[affinity.repr]) != r - affinity.offset) {

            cost++;
         }
      }

      for (unsigned c = 0; c < size; ++c) {
         unsigned j = r + c;

         /* If the register is unavailable, account for the cost of shuffling */
         if (!BITSET_TEST(ra->available_regs[file], j) && !tied) {
            bool live_out = u_sparse_bitset_test(&ra->block->live_out,
                                                 ra->index_for_reg[file][j]);
            cost += 1 + live_out;
         }

         /* Model the cost of shuffling for phis */
         struct phi_web_node *phi_web =
            &ra->phi_web[phi_web_find(ra->phi_web, jay_channel(var, c))];
         if (phi_web->reg != NO_REG && r_reg(phi_web->reg) != j) {
            cost += 2;
         }

         /* Choosing this register will pin it, leaving it unavailable to later
          * smaller sources which will need a move.
          */
         cost += BITSET_TEST(ra->sources[file], j);
      }

      if (cost < *best_cost) {
         *best_cost = cost;
         *best_reg = r;

         /* If we find something with 0 cost, we are guaranteed to pick this
          * register, so terminate early. This speeds up the search.
          */
         if (cost == 0) {
            return;
         }
      }
   }
}

static unsigned
pick_regs(jay_ra_state *ra,
          enum jay_file file,
          unsigned size,
          unsigned alignment,
          enum jay_stride min_stride,
          enum jay_stride max_stride,
          jay_inst *I,
          jay_def var,
          bool is_src)
{
   struct jay_partition *partition = &ra->b.shader->partition;
   bool eot = jay_is_early_eot_send(ra->b.shader, I);

   /* If possible, keep sources in place to avoid shuffles. */
   if (is_src && jay_channel(var, 0) != 0) {
      unsigned cur = r_reg(ra->reg_for_index[jay_channel(var, 0)]);
      struct jay_register_block block = jay_lookup_block(partition, cur, file);

      if (!BITSET_TEST_COUNT(ra->pinned[file], cur, size) &&
          util_is_aligned(cur - block.start_gpr, alignment) &&
          is_block_compatible(block, file, min_stride, max_stride, eot,
                              false) &&
          cur + size <= (block.start_gpr + block.len_gpr)) {
         return cur;
      }
   }

   unsigned best_cost = UINT32_MAX;
   unsigned best_reg = 0;
   struct affinity affinity =
      ra->phi_web[phi_web_find(ra->phi_web, jay_channel(var, 0))].affinity;

   assert(alignment >= size && "alignment must be a multiple of size");

   /* We select registers roundrobin. This has several benefits:
    *
    * 1. Easier coalescing since we are less likely statistically to allocate
    *    a register that a future instruction has an affinity.
    *
    * 2. More freedom for post-RA scheduling thanks to fewer dependencies.
    *
    * 3. Less stalling due to SWSB annotations from register reuse.
    */
   enum jay_stride stride = file == GPR ? min_stride : 0;
   struct jay_roundrobin *rr = &ra->roundrobin[file][stride];
   unsigned nr_blocks = partition->nr_blocks[file];

   /* Make sure we use the optimal stride for roundrobin RA */
   if (file == GPR) {
      for (unsigned i = 0; i < nr_blocks; ++i) {
         if (partition->blocks[GPR][rr->block].stride == stride) {
            break;
         } else {
            rr->block = (rr->block + 1 == nr_blocks) ? 0 : rr->block + 1;
         }
      }
   }

   unsigned last_b_ = rr->block + nr_blocks;
   for (unsigned b_ = rr->block; b_ <= last_b_ && best_cost > 0; ++b_) {
      unsigned b = b_ >= nr_blocks ? (b_ - nr_blocks) : b_;
      assert(b < nr_blocks);

      struct jay_register_block block = partition->blocks[file][b];

      if (is_block_compatible(block, file, min_stride, max_stride, eot,
                              false)) {
         unsigned r = b_ == rr->block ? rr->gpr : 0;

         if (affinity.repr == jay_channel(var, 0) && b_ == rr->block) {
            r += affinity.offset;
         }

         /* Assigning a stride that is too big may result in SIMDness splitting.
          * Model that cost so we prefer packed registers.
          */
         unsigned block_cost = file == GPR ? block.stride - min_stride : 0;

         /* If we are used for end-of-thread and it is not in the appropriate
          * register, we will need to insert 1 copy per channel at the end.
          */
         if (affinity.eot && block.type != JAY_BLOCK_EOT) {
            block_cost += size;
         }

         /* Consider only blocks that could be picked */
         if (best_cost > block_cost) {
            pick_regs_from_block(ra, file, size, alignment, I, var, is_src,
                                 block, block_cost, affinity, &best_cost,
                                 &best_reg, r);
         }
      }
   }

   /* If we chose a register roundrobin (the constant 16 here is determined
    * experimentally), advance the roundrobin. As a heuristic, advance by a
    * whole vector if we are the representative. This leaves us registers for
    * the rest of the vector.
    */
   if (rr->gpr <= best_reg && best_reg <= rr->gpr + 16) {
      bool is_repr = affinity.repr == jay_channel(var, 0);
      rr->gpr = best_reg + MAX2(size, is_repr ? affinity.nr : 0);

      if (rr->gpr >= partition->blocks[file][rr->block].len_gpr) {
         rr->block = ((rr->block + 1) == nr_blocks) ? 0 : (rr->block + 1);
         rr->gpr = 0;
      }
   }

   assert(best_cost != UINT32_MAX && "we always find something");
   assert(best_reg + size <= ra->num_regs[file]);
   return best_reg;
}

static void
assign_regs_for_inst(jay_ra_state *ra, jay_inst *I)
{
   jay_shader *shader = ra->b.shader;
   jay_def *vars[JAY_MAX_OPERANDS];
   jay_def saved_srcs[JAY_MAX_SRCS];
   struct jay_parallel_copy copies[JAY_MAX_DEF_LENGTH * JAY_MAX_OPERANDS];
   uint32_t eviction_indices[JAY_MAX_DEF_LENGTH * JAY_MAX_OPERANDS];
   unsigned nr_vars = 0, nr_copies = 0;

   /* Gather temporary registers that are free /before/ any shuffling */
   struct jay_temp_regs temp_regs = find_temp_regs(ra);

   /* Save sources so we can get at last-use info even after munging */
   typed_memcpy(saved_srcs, I->src, I->num_srcs);

   /* Gather sources (in order) then destinations. This order (with a stable
    * sort) ensures we see killed sources before same-size destinations,
    * naturally tying the last source to the destination. Predicated default
    * values rely on this invariant for correctness.
    */
   jay_foreach_ra_src(I, s) {
      /* Filter out duplicate scalar sources - they should only be assigned
       * once. Duplicated vector sources are lowered away as a precondition.
       */
      bool duplicate = false;
      if (jay_num_values(I->src[s]) == 1) {
         uint32_t index = jay_index(I->src[s]);

         for (unsigned i = 0; i < nr_vars; ++i) {
            jay_def var = *(vars[i]);
            duplicate |= (jay_num_values(var) == 1 && jay_index(var) == index);
         }
      }

      if (!duplicate) {
         vars[nr_vars++] = &I->src[s];

         /* Record the old registers as parallel copies to be filled in later.
          * Then release the old registers to be reassigned.
          */
         jay_foreach_index(I->src[s], _, index) {
            jay_reg reg = current_reg(ra, index);
            BITSET_SET(ra->sources[r_file(reg)], r_reg(reg));

            eviction_indices[nr_copies] = index;
            copies[nr_copies++] = (struct jay_parallel_copy) { .src = reg };
            release_reg(ra, reg);
         }
      }
   }

   if (!jay_is_null(I->dst) && I->dst.file < JAY_NUM_RA_FILES) {
      vars[nr_vars++] = &I->dst;
   }

   /* Sort variables by size in descending order. We use insertion sort
    * because it is stable, adaptive, and faster than mergesort for small n.
    *
    * Algorithm from CLRS.
    */
   for (unsigned i = 1; i < nr_vars; ++i) {
      jay_def *pivot = vars[i];
      unsigned j, key = pivot->num_values_m1;

      for (j = i; j > 0 && key > vars[j - 1]->num_values_m1; --j) {
         vars[j] = vars[j - 1];
      }

      vars[j] = pivot;
   }

   /* Partition `copies` into "source shuffles" and "livethrough shuffles" */
   uint32_t first_eviction_copy = nr_copies;

   /* Choose registers for sources/destinations in order */
   for (unsigned i = 0; i < nr_vars; ++i) {
      bool is_src = vars[i] >= I->src;
      bool killed = false;
      jay_def var = *(vars[i]);
      unsigned size = jay_num_values(var);
      unsigned alignment =
         I->op == JAY_OPCODE_EXPAND_QUAD ? 1 : util_next_power_of_two(size);
      enum jay_file file = var.file;
      enum jay_stride min_stride = JAY_STRIDE_2, max_stride = JAY_STRIDE_8;

      assert(size > 0 && file < JAY_NUM_RA_FILES && "filtered above");

      if (is_src) {
         /* If a source is duplicated, we need to take the most constrained
          * version. This matters for 3-src restrictions.
          */
         jay_foreach_src(I, s) {
            if (jay_defs_equivalent(var, I->src[s])) {
               alignment = MAX2(alignment, jay_src_alignment(shader, I, s));
               min_stride =
                  MAX2(jay_src_stride_minmax(I, s, false), min_stride);
               max_stride = MIN2(jay_src_stride_minmax(I, s, true), max_stride);
            }
         }

         unsigned s = vars[i] - I->src;

         /* Sources are considered killed only if completely killed */
         unsigned lu = jay_source_last_use_bit(saved_srcs, s);

         killed = true;
         for (unsigned i = 0; i < size; ++i) {
            if (jay_channel(I->src[s], i) == 0 ||
                !BITSET_TEST(I->last_use, lu + i)) {
               killed = false;
               break;
            }
         }

         jay_foreach_index(var, c, index) {
            BITSET_CLEAR(ra->sources[file], r_reg(ra->reg_for_index[index]));
         }
      } else {
         alignment = MAX2(alignment, jay_dst_alignment(shader, I));
         min_stride = jay_dst_stride_minmax(I, false);
         max_stride = jay_dst_stride_minmax(I, true);
      }

      /* Choose registers satisfying the constraints and minimizing shuffles */
      unsigned base = pick_regs(ra, file, size, alignment, min_stride,
                                max_stride, I, var, is_src);
      jay_reg reg = make_reg(file, base);

      /* If we decided to tie, process that */
      if (!is_src && BITSET_TEST(ra->killed[file], base)) {
         unsigned found = ~0;
         for (unsigned j = 0; j < i; ++j) {
            if (vars[j]->file == file && vars[j]->reg == base) {
               found = j;
               break;
            }
         }

         assert(found < i && vars[found] >= I->src && "killed source");
         unsigned lu_offs =
            jay_source_last_use_bit(saved_srcs, vars[found] - I->src);

         /* Fully killed source so we can zero a contiguous range. Note we need
          * to use the unpadded size to avoid leaking a register for vec3
          * destinations tied to vec4 sources.
          */
         BITSET_CLEAR_COUNT(I->last_use, lu_offs, jay_num_values(var));
         BITSET_CLEAR(ra->killed[file], base);
      } else {
         /* Otherwise pin our choice */
         BITSET_SET_COUNT(ra->pinned[file], base, size);

         for (unsigned c = 0; c < size; ++c) {
            /* Evict any livethrough value interfering with our choice */
            if (!(is_src && jay_channel(var, c) == 0) &&
                !reg_is_available(ra, reg + c)) {
               uint32_t index = ra->index_for_reg[file][base + c];
               struct jay_parallel_copy copy = { .src = reg + c };
               eviction_indices[nr_copies] = index;
               copies[nr_copies++] = copy;
               release_reg(ra, reg + c);
            }
         }
      }

      jay_set_reg(vars[i], base);

      jay_foreach_index(var, c, index) {
         assign_reg_for_index(ra, index, reg + c);
      }

      if (killed) {
         BITSET_SET(ra->killed[file], vars[i]->reg);
      }
   }

   /* Set .reg late so duplicated scalar sources are handled properly */
   jay_foreach_ra_src(I, s) {
      if (I->src[s]._payload != JAY_SENTINEL) {
         jay_set_reg(&I->src[s],
                     r_reg(ra->reg_for_index[jay_channel(I->src[s], 0)]));
      }
   }

   /* Look up where shuffled sources ended up */
   for (unsigned i = 0; i < first_eviction_copy; ++i) {
      copies[i].dst = ra->reg_for_index[eviction_indices[i]];
   }

   /* Assign new registers for evicted values */
   for (unsigned i = first_eviction_copy; i < nr_copies; ++i) {
      copies[i].dst = find_free_reg(ra, r_file(copies[i].src), ~0);
      assign_reg_for_index(ra, eviction_indices[i], copies[i].dst);
   }

   /* Shuffle everything */
   ra->b.cursor = jay_before_inst(I);
   jay_emit_parallel_copies(&ra->b, copies, nr_copies, temp_regs);

   /* Reset data structures */
   for (unsigned i = 0; i < nr_vars; ++i) {
      jay_def var = *(vars[i]);
      BITSET_CLEAR_COUNT(ra->pinned[var.file], var.reg, jay_num_values(var));
      BITSET_CLEAR_COUNT(ra->killed[var.file], var.reg, jay_num_values(var));
   }

   /* Sources selected for early-kill have had their last_use fields cleared.
    * Anything else is late-killed. Release those registers.
    */
   unsigned kill_idx = 0;
   jay_foreach_ssa_src(I, s) {
      jay_foreach_index(saved_srcs[s], c, idx) {
         if (I->src[s].file < JAY_NUM_RA_FILES &&
             BITSET_TEST(I->last_use, kill_idx)) {

            release_reg(ra, make_reg(I->src[s].file, I->src[s].reg + c));
         }

         kill_idx++;
      }
   }
}

static void
local_ra(jay_ra_state *ra, jay_block *block)
{
   ra->block = block;

   /* Initialize local data structures based on global state */
   jay_foreach_ra_file(file) {
      BITSET_SET_COUNT(ra->available_regs[file], 0, ra->num_regs[file]);
   }

   U_SPARSE_BITSET_FOREACH_SET(&block->live_in, i) {
      if (ra->global_reg_for_index[i] != NO_REG) {
         assign_reg_for_index(ra, i, ra->global_reg_for_index[i]);
      }
   }

   /* Assign registers locally */
   jay_foreach_inst_in_block(block, I) {
      if (I->op == JAY_OPCODE_PHI_SRC) {
         break;
      } else if (I->op == JAY_OPCODE_PHI_DST) {
         /* Phis are special as we never shuffle them */
         unsigned index = jay_index(I->dst);
         jay_reg reg = ra->phi_web[phi_web_find(ra->phi_web, index)].reg;

         if (reg == NO_REG || !reg_is_available(ra, reg)) {
            reg = find_free_reg(ra, I->dst.file, ~0);
         }

         assign_reg_for_index(ra, jay_index(I->dst), reg);
         I->dst.reg = r_reg(reg);
      } else if (I->op == JAY_OPCODE_PRELOAD) {
         /* Preloads always get what they want */
         I->dst.reg = jay_preload_reg(I);
         jay_reg base = make_reg(I->dst.file, I->dst.reg);

         jay_foreach_comp(I->dst, c) {
            assert(reg_is_available(ra, base + c) && "preloads always work");
            assign_reg_for_index(ra, jay_channel(I->dst, c), base + c);
         }
      } else {
         /* For normal instructions, assign registers. */
         assign_regs_for_inst(ra, I);
      }

      /* Release registers for destinations that are immediately killed */
      jay_foreach_index(I->dst, _, index) {
         if (BITSET_TEST(ra->b.func->dead_defs, index)) {
            release_reg(ra, current_reg(ra, index));
         }
      }

      if (jay_debug & JAY_DBG_PRINTDEMAND) {
         printf("(RA) [G:%u\tU:%u] ", register_demand(ra, GPR),
                register_demand(ra, UGPR));
         jay_print_inst(stdout, I);
      }
   }

   /* Gather temporary registers that are free /before/ any shuffling */
   struct jay_temp_regs temp_regs = find_temp_regs(ra);

   /* Reconcile local state with the global structures */
   jay_foreach_ra_file(file) {
      BITSET_SET_COUNT(ra->available_regs[file], 0, ra->num_regs[file]);
   }

   /* Extend live ranges for correctness. Might be a better solution though. */
   jay_foreach_inst_in_block_rev(block, I) {
      if (I->op != JAY_OPCODE_PHI_SRC && !jay_op_is_control_flow(I->op)) {
         break;
      }

      jay_foreach_ra_src(I, s) {
         u_sparse_bitset_set(&block->live_out, jay_index(I->src[s]));
      }
   }

   /* Already assigned global registers need to be shuffled back */
   struct util_dynarray copies = UTIL_DYNARRAY_INIT;

   U_SPARSE_BITSET_FOREACH_SET(&block->live_out, i) {
      jay_reg lreg = ra->reg_for_index[i], greg = ra->global_reg_for_index[i];

      if (lreg != NO_REG && greg != NO_REG) {
         add_copy(&copies, greg, lreg);
         assign_reg_for_index(ra, i, greg);
      }
   }

   /* Live-out variables defined in this block need global registers assigned */
   U_SPARSE_BITSET_FOREACH_SET(&block->live_out, i) {
      jay_reg reg = ra->reg_for_index[i];

      if (ra->global_reg_for_index[i] == NO_REG && reg != NO_REG) {
         if (!reg_is_available(ra, reg)) {
            jay_reg old = reg;
            reg = find_free_reg(ra, r_file(reg), ~0);
            add_copy(&copies, reg, old);
         }

         assign_reg_for_index(ra, i, reg);
         ra->global_reg_for_index[i] = reg;
      }
   }

   /* Gather temporary registers free after shuffling (before phis) */
   block->temps_out = find_temp_regs(ra);

   /* Handle the end of the block */
   ra->b.cursor = jay_before_block(block);

   jay_foreach_inst_in_block_rev(block, I) {
      if (I->op != JAY_OPCODE_PHI_SRC && !jay_op_is_control_flow(I->op)) {
         ra->b.cursor = jay_after_inst(I);
         break;
      }

      jay_foreach_ra_src(I, s) {
         jay_set_reg(&I->src[s],
                     r_reg(ra->global_reg_for_index[jay_index(I->src[s])]));
      }
   }

   const unsigned num_pcopies =
      util_dynarray_num_elements(&copies, struct jay_parallel_copy);

   jay_emit_parallel_copies(&ra->b, copies.data, num_pcopies, temp_regs);
   util_dynarray_fini(&copies);
}

/*
 * Record all phi webs. First initialize the union-find data structure
 * with all SSA defs in their own singletons, then union together anything
 * related by a phi. The resulting union-find structure will be the webs.
 *
 * As a heuristic, we skip the union if the phi source interferes with the phi
 * destination (equivalently: the phi source is live-out of the source block).
 * These phis could never be coalesced, so the union can only hurt (and it does
 * in practice in complex web scenarios). Note this case is only possible
 * because we do not lower the input program to conventional SSA (CSSA) form.
 */
static void
construct_phi_webs(struct phi_web_node *web, jay_function *f)
{
   for (unsigned i = 0; i < f->ssa_alloc; ++i) {
      web[i] = (struct phi_web_node) { .parent = i, .reg = NO_REG };
   }

   jay_foreach_block(f, block) {
      jay_foreach_phi_src_in_block(block, phi) {
         if (!u_sparse_bitset_test(&block->live_out, jay_index(phi->src[0]))) {
            phi_web_union(web, jay_index(phi->src[0]), jay_phi_src_index(phi));
         }
      }
   }
}

static void
insert_parallel_copies_for_phis(jay_function *f)
{
   jay_reg *phi_dsts = malloc(f->ssa_alloc * sizeof(jay_reg));
   struct util_dynarray copies = UTIL_DYNARRAY_INIT;
   memset(phi_dsts, 0xFF, sizeof(jay_reg) * f->ssa_alloc);

   jay_foreach_block(f, block) {
      jay_foreach_phi_dst_in_block(block, I) {
         phi_dsts[jay_index(I->dst)] = make_reg(I->dst.file, I->dst.reg);
      }
   }

   jay_foreach_block(f, block) {
      jay_builder b = jay_init_builder(f, jay_before_jump(block));

      /* Copy phi source to phi destination along the edge. */
      jay_foreach_phi_src_in_block(block, phi) {
         jay_reg src = make_reg(phi->src[0].file, phi->src[0].reg);
         add_copy(&copies, phi_dsts[jay_phi_src_index(phi)], src);
         jay_remove_instruction(phi);
      }

      const unsigned nr =
         util_dynarray_num_elements(&copies, struct jay_parallel_copy);

      jay_emit_parallel_copies(&b, copies.data, nr, block->temps_out);
      util_dynarray_clear(&copies);
   }

   util_dynarray_fini(&copies);
   free(phi_dsts);
}

static void
map_gpr_to_acc(jay_shader *shader, jay_def *x)
{
   if (x->file == GPR) {
      struct jay_register_block B =
         jay_lookup_block(&shader->partition, x->reg, GPR);

      if (B.type == JAY_BLOCK_ACCUM) {
         x->file = ACCUM;
         x->reg = (2 + (x->reg - B.start_gpr)) * 2;
      }
   }
}

static void
jay_register_allocate_function(jay_function *f)
{
   jay_shader *shader = f->shader;
   jay_ra_state ra = { .b.shader = shader, .b.func = f };
   typed_memcpy(ra.num_regs, shader->num_regs, JAY_NUM_RA_FILES);

   linear_ctx *lin_ctx = linear_context(shader);
   ra.reg_for_index = linear_alloc_array(lin_ctx, jay_reg, f->ssa_alloc);
   ra.global_reg_for_index = linear_alloc_array(lin_ctx, jay_reg, f->ssa_alloc);
   ra.affinities = linear_zalloc_array(lin_ctx, struct affinity, f->ssa_alloc);

   memset(ra.reg_for_index, 0xFF, sizeof(jay_reg) * f->ssa_alloc);
   memset(ra.global_reg_for_index, 0xFF, sizeof(jay_reg) * f->ssa_alloc);

   jay_foreach_ra_file(file) {
      const unsigned num_regs = ra.num_regs[file];
      ra.index_for_reg[file] = linear_zalloc_array(lin_ctx, uint32_t, num_regs);
      ra.available_regs[file] = BITSET_LINEAR_ZALLOC(lin_ctx, num_regs);
      ra.pinned[file] = BITSET_LINEAR_ZALLOC(lin_ctx, num_regs);
      ra.killed[file] = BITSET_LINEAR_ZALLOC(lin_ctx, num_regs);
      ra.sources[file] = BITSET_LINEAR_ZALLOC(lin_ctx, num_regs);
   }

   ra.phi_web = linear_alloc_array(lin_ctx, struct phi_web_node, f->ssa_alloc);

   /* Construct the phi equivalence classes using the union-find data
    * structure. This associates all SSA values related to the same phi,
    * and selects one of them as a canonical/representative value.
    */
   construct_phi_webs(ra.phi_web, f);

   /* We track the order of instructions in the program to inform coalescing */
   uint32_t *order = linear_alloc_array(lin_ctx, uint32_t, f->ssa_alloc);
   uint32_t order_counter = 0;

   jay_foreach_inst_in_func(f, block, I) {
      jay_foreach_dst_index(I, _, index) {
         order[index] = order_counter++;
      }

      jay_foreach_src_index(I, s, c, index) {
         /* We check repr==0 to try to coalesce with the first vector use, as
          * the closest to the definition. This heuristic reduces shuffling.
          */
         if (jay_num_values(I->src[s]) > 1 && !ra.affinities[index].repr) {
            uint32_t repr = UINT_MAX, repr_c = 0, best_order = UINT_MAX;

            /* Pick the earliest representative to maximize freedom */
            jay_foreach_index(I->src[s], j, index) {
               if (order[index] < best_order) {
                  repr = index;
                  repr_c = j;
                  best_order = order[index];
               }
            }

            ra.affinities[index].repr = repr;
            ra.affinities[index].offset = repr == index ? c : c - repr_c;
            ra.affinities[index].nr = MIN2(jay_num_values(I->src[s]), 15);
         }

         if (jay_is_early_eot_send(shader, I)) {
            ra.affinities[index].eot = true;
         }

         unsigned al = jay_src_alignment(shader, I, s);
         al = MAX2(al, util_next_power_of_two(jay_num_values(I->src[s])));

         if (al >= ra.affinities[index].align) {
            ra.affinities[index].align = al;
            ra.affinities[index].align_offs = c;
         }

         ra.phi_web[phi_web_find(ra.phi_web, index)].affinity =
            ra.affinities[index];
      }
   }

   jay_foreach_block(f, block) {
      local_ra(&ra, block);
   }

   linear_free_context(lin_ctx);

   /* Validate the registers we picked before going out of SSA */
   jay_validate_ra(f);

   insert_parallel_copies_for_phis(f);

   if (f->demand[MEM]) {
      jay_lower_spill(f);
   }

   jay_foreach_inst_in_func(f, block, I) {
      map_gpr_to_acc(shader, &I->dst);

      jay_foreach_src(I, s) {
         map_gpr_to_acc(shader, &I->src[s]);
      }
   }
}

void
jay_register_allocate(jay_shader *s)
{
   jay_foreach_function(s, f) {
      jay_register_allocate_function(f);
   }

   s->post_ra = true;
}
