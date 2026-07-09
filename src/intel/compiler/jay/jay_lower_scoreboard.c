/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include "compiler/brw/brw_eu_defines.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

#define NUM_TOKENS (32)

struct key {
   unsigned base, width;
};

static inline struct key
def_to_regdist_key(jay_function *func, jay_inst *I, jay_def x)
{
   if (x.file == GPR || x.file == UGPR) {
      unsigned base = x.file == UGPR ? func->shader->num_regs[GPR] : 0;
      return (struct key) { base + x.reg, jay_num_values(x) };
   } else if (x.file == ACCUM || x.file == UACCUM) {
      unsigned base =
         func->shader->num_regs[GPR] + func->shader->num_regs[UGPR];

      return (struct key) { base + (x.reg / 2), jay_num_values(x) };
   } else {
      return (struct key) { 0, 0 };
   }
}

static inline struct key
def_to_sbid_key(jay_function *func, jay_inst *I, jay_def x)
{
   if (x.file == GPR) {
      return (struct key) { x.reg, jay_num_values(x) };
   } else if (x.file == UGPR) {
      /* SEND instructions can only use GRF-aligned multiples of whole
       * registers, so there's no point tracking UGPRs at a finer granularity.
       */
      return (struct key) {
         func->shader->num_regs[GPR] + x.reg / jay_ugpr_per_grf(func->shader),
         DIV_ROUND_UP(jay_num_values(x), jay_ugpr_per_grf(func->shader))
      };
   } else {
      return (struct key) { 0, 0 };
   }
}

enum sbid_dep_type { SRC, DST, MAX_SBID_DEP_TYPES };

#define jay_foreach_sbid_dep_type(type)                                        \
   for (enum sbid_dep_type type = SRC; type < MAX_SBID_DEP_TYPES; ++type)

struct swsb_sbid_state;

struct swsb_sbid_edge {
   struct swsb_sbid_state *ctx;
   uint32_t tokens_busy[MAX_SBID_DEP_TYPES];
   BITSET_WORD *tokens_bitset[NUM_TOKENS];
};

/** SBID scoreboarding */
struct swsb_sbid_state {
   unsigned words;
   unsigned max_sbids;

   void *mem_ctx;
   linear_ctx *lin_ctx;
   struct util_dynarray bitset_pool;

   struct swsb_sbid_edge *edges;
};

static inline BITSET_WORD *
bitset_for(const struct swsb_sbid_edge *edge,
           unsigned sbid,
           enum sbid_dep_type type)
{
   assert(edge->tokens_bitset[sbid] != NULL);
   return &edge->tokens_bitset[sbid][type * edge->ctx->words];
}

/** Initializes the swsb_sbid_state struct */
static inline void
init_sbid_state(struct swsb_sbid_state *sbid_state,
                unsigned num_blocks,
                uint32_t nr_sbid_keys,
                unsigned max_sbids)
{
   *sbid_state = (struct swsb_sbid_state) {
      .words = BITSET_WORDS(nr_sbid_keys),
      .max_sbids = max_sbids,
      .mem_ctx = ralloc_context(NULL),
   };

   sbid_state->lin_ctx = linear_context(sbid_state->mem_ctx);
   util_dynarray_init(&sbid_state->bitset_pool, sbid_state->mem_ctx);

   sbid_state->edges = linear_zalloc_array(sbid_state->lin_ctx,
                                           struct swsb_sbid_edge, num_blocks);
   for (unsigned i = 0; i < num_blocks; ++i)
      sbid_state->edges[i].ctx = sbid_state;
}

static inline void store_sbid_edge(struct swsb_sbid_edge *edge,
                                   const struct swsb_sbid_edge *src);

/** Resets the swsb_sbid_state struct */
static inline void
clear_sbid_state(struct swsb_sbid_state *sbid_state, unsigned dirty_blocks)
{
   if (dirty_blocks > 0) {
      for (unsigned i = 0; i < dirty_blocks; ++i) {
         store_sbid_edge(&sbid_state->edges[i], NULL);
      }
   }
}

/** Allocate a zero'd bitset from the pool */
static inline BITSET_WORD *
alloc_sbid_bitset(struct swsb_sbid_state *ctx)
{
   if (!ctx->bitset_pool.size) {
      return linear_zalloc_child(ctx->lin_ctx,
                                 ctx->words * sizeof(BITSET_WORD) * 2);
   }
   BITSET_WORD *ret = util_dynarray_pop(&ctx->bitset_pool, BITSET_WORD *);
   assert(__bitset_is_empty(ret, ctx->words * 2));
   return ret;
}

/** Release a zero'd bitset back to the pool */
static inline void
release_sbid_bitset(struct swsb_sbid_state *ctx, BITSET_WORD **bitset)
{
   assert(__bitset_is_empty(*bitset, ctx->words * 2));
   util_dynarray_append(&ctx->bitset_pool, *bitset);
   *bitset = NULL;
}

/** Validates that a swsb_sbid_edge isn't in a bad state */
#define validate_edge(edge)                                                    \
   do {                                                                        \
      if (!(edge))                                                             \
         break;                                                                \
      uint32_t tokens_alloced =                                                \
         (edge)->tokens_busy[SRC] | (edge)->tokens_busy[DST];                  \
      assert(!((edge)->tokens_busy[SRC] & ~(edge)->tokens_busy[DST]));         \
      for (unsigned sbid = 0; sbid < NUM_TOKENS; ++sbid) {                     \
         assert(((edge)->tokens_bitset[sbid] != NULL) ==                       \
                !!(tokens_alloced & BITFIELD_BIT(sbid)));                      \
      }                                                                        \
      jay_foreach_sbid_dep_type(type) {                                        \
         u_foreach_bit(sbid, tokens_alloced & ~(edge)->tokens_busy[type]) {    \
            assert(__bitset_is_empty(bitset_for((edge), sbid, type),           \
                                     (edge)->ctx->words));                     \
         }                                                                     \
      }                                                                        \
   } while (false)

/** Copies the state of an swsb_sbid_edge struct */
static inline void
store_sbid_edge(struct swsb_sbid_edge *dst, const struct swsb_sbid_edge *src)
{
   validate_edge(src);
   uint32_t src_alloced = 0, dst_alloced = 0;

   jay_foreach_sbid_dep_type(type) {
      src_alloced |= src ? src->tokens_busy[type] : 0;
      dst_alloced |= dst->tokens_busy[type];
   }

   u_foreach_bit(sbid, src_alloced & ~dst_alloced) {
      dst->tokens_bitset[sbid] = alloc_sbid_bitset(dst->ctx);
   }

   jay_foreach_sbid_dep_type(type) {
      uint32_t src_busy = src ? src->tokens_busy[type] : 0;
      uint32_t dst_busy = dst->tokens_busy[type];

      u_foreach_bit(sbid, src_busy) {
         __bitset_copy(bitset_for(dst, sbid, type), bitset_for(src, sbid, type),
                       dst->ctx->words);
      }

      u_foreach_bit(sbid, ~src_busy & dst_busy) {
         __bitset_zero(bitset_for(dst, sbid, type), dst->ctx->words);
      }

      dst->tokens_busy[type] = src_busy;
   }

   u_foreach_bit(sbid, dst_alloced & ~src_alloced) {
      release_sbid_bitset(dst->ctx, &dst->tokens_bitset[sbid]);
   }

   validate_edge(dst);
}

/** Merges two edges together and stores the result in out */
static inline void
merge_sbid_edges(const struct swsb_sbid_edge *a,
                 const struct swsb_sbid_edge *b,
                 struct swsb_sbid_edge *out)
{
   validate_edge(a);
   validate_edge(b);
   uint32_t src_alloced = 0, dst_alloced = 0;

   jay_foreach_sbid_dep_type(dep) {
      src_alloced |= a->tokens_busy[dep] | b->tokens_busy[dep];
      dst_alloced |= out->tokens_busy[dep];
   }

   u_foreach_bit(sbid, src_alloced & ~dst_alloced) {
      out->tokens_bitset[sbid] = alloc_sbid_bitset(out->ctx);
   }

   jay_foreach_sbid_dep_type(type) {
      uint32_t a_has = a->tokens_busy[type];
      uint32_t b_has = b->tokens_busy[type];
      uint32_t dst_has = out->tokens_busy[type];

      u_foreach_bit(sbid, a_has & b_has) {
         __bitset_or(bitset_for(out, sbid, type),
                     bitset_for(a, sbid, type),
                     bitset_for(b, sbid, type),
                     out->ctx->words);
      }

      u_foreach_bit(sbid, a_has ^ b_has) {
         __bitset_copy(bitset_for(out, sbid, type),
                       bitset_for(a_has & BITFIELD_BIT(sbid) ? a : b, sbid,
                                  type),
                       out->ctx->words);
      }

      u_foreach_bit(sbid, ~a_has & ~b_has & dst_has) {
         __bitset_zero(bitset_for(out, sbid, type), out->ctx->words);
      }

      out->tokens_busy[type] = a_has | b_has;
   }

   u_foreach_bit(sbid, dst_alloced & ~src_alloced) {
      release_sbid_bitset(out->ctx, &out->tokens_bitset[sbid]);
   }

   validate_edge(out);
}

/**
 * Merges the edges of each of the block's predecessors together and stores
 * the result in edge_out
 */
static inline void
merge_sbid_edges_of_preds(struct swsb_sbid_state *state,
                          jay_block *block,
                          struct swsb_sbid_edge *edge_out)
{
   struct swsb_sbid_edge *accum = NULL;
   jay_foreach_predecessor(block, pred, UGPR) {
      struct swsb_sbid_edge *edge = &state->edges[(*pred)->index];

      if (accum == NULL) {
         accum = edge;
      } else {
         merge_sbid_edges(accum, edge, edge_out);
         accum = edge_out;
      }
   }

   if (edge_out != accum) {
      assert(jay_num_predecessors(block, UGPR) < 2 && "the no-merge case");
      store_sbid_edge(edge_out, accum);
   }
}

static inline unsigned
hash_block_index(unsigned index)
{
   index ^= index >> 16;
   index *= 0x85ebca6b;
   index ^= index >> 13;
   index *= 0xc2b2ae35;
   index ^= index >> 16;
   return index;
}

static inline void
sync_sbids(jay_builder *b, uint32_t mask, gen_sbid_mode mode)
{
   if (util_is_power_of_two_nonzero(mask)) {
      jay_SYNC(b, jay_null(), TGL_SYNC_NOP)->dep =
         gen_swsb_sbid(mode, util_logbase2(mask));
   } else if (mask) {
      jay_SYNC(b, mask, mode == GEN_SBID_DST ? TGL_SYNC_ALLWR : TGL_SYNC_ALLRD);
   }
}

static inline bool
jay_inst_has_sbid(const jay_inst *I)
{
   return jay_inst_is_unordered(I) &&
          !(I->op == JAY_OPCODE_SEND && jay_send_eot(I));
}

static inline unsigned
jay_inst_sbid(const jay_inst *I)
{
   return I->op == JAY_OPCODE_SEND ? jay_send_sbid(I) : jay_dpas_sbid(I);
}

static inline void
jay_inst_set_sbid(jay_inst *I, unsigned sbid)
{
   if (I->op == JAY_OPCODE_SEND)
      jay_set_send_sbid(I, sbid);
   else
      jay_set_dpas_sbid(I, sbid);
}

/**
 * Returns the index of the Nth zero-indexed bit set in the bitfield. Such a bit
 * must exist. For example, using this function to find with bit=1 bit set in
 * the bitfield 0b1100010 would return 5.
 */
static inline unsigned
find_nth_bit(uint32_t bitfield, unsigned bit)
{
   assert(bit < util_bitcount(bitfield) && "must exist");

   /* Repeatedly clear the bottom bit */
   for (unsigned i = 0; i < bit; ++i) {
      bitfield &= (bitfield - 1);
   }

   return ffs(bitfield) - 1;
}

static void
lower_sbid_local(jay_function *func,
                 jay_block *block,
                 struct swsb_sbid_edge *edge,
                 bool commit)
{
   validate_edge(edge);

   uint32_t busy_src = edge->tokens_busy[SRC];
   uint32_t busy_dst = edge->tokens_busy[DST];

   unsigned roundrobin = 0;

   /* Try to reduce conflicts across control flow edges by selecting a random
    * number to XOR with the roundrobin counter.
    */
   unsigned hash = hash_block_index(block->index);

   jay_foreach_inst_in_block_safe(block, I) {
      jay_builder b = jay_init_builder(func, jay_before_inst(I));
      uint32_t sync_src = 0, sync_dst = 0;

      /* Read-after-write */
      jay_foreach_src(I, s) {
         struct key src = def_to_sbid_key(func, I, I->src[s]);

         u_foreach_bit(sbid, busy_dst) {
            if (BITSET_TEST_COUNT(bitset_for(edge, sbid, DST), src.base,
                                  src.width)) {
               sync_dst |= BITFIELD_BIT(sbid);
               busy_dst &= ~BITFIELD_BIT(sbid);
               busy_src &= ~BITFIELD_BIT(sbid);
            }
         }
      }

      /* Write-after-write & write-after-read */
      jay_foreach_dst(I, d) {
         struct key dst = def_to_sbid_key(func, I, d);

         u_foreach_bit(sbid, busy_dst) {
            if (BITSET_TEST_COUNT(bitset_for(edge, sbid, DST), dst.base,
                                  dst.width)) {
               sync_dst |= BITFIELD_BIT(sbid);
               busy_dst &= ~BITFIELD_BIT(sbid);
               busy_src &= ~BITFIELD_BIT(sbid);
            }
         }

         u_foreach_bit(sbid, busy_src) {
            if (BITSET_TEST_COUNT(bitset_for(edge, sbid, SRC), dst.base,
                                  dst.width)) {
               sync_src |= BITFIELD_BIT(sbid);
               busy_src &= ~BITFIELD_BIT(sbid);
            }
         }
      }

      if (jay_inst_has_sbid(I)) {
         unsigned sbid;

         if (commit) {
            sbid = jay_inst_sbid(I);
         } else {
            if (sync_dst) {
               /* If we depend on $N.dst, there's no extra cost to $N.set */
               sbid = ffs(sync_dst) - 1;
            } else {
               /* Otherwise, select a random SBID that's not already busy */
               unsigned max_sbids = edge->ctx->max_sbids;
               uint32_t free_sbids = ~busy_dst & BITFIELD_MASK(max_sbids);
               if (free_sbids) {
                  sbid = (roundrobin++ ^ hash) % util_bitcount(free_sbids);
                  sbid = find_nth_bit(free_sbids, sbid);
               } else {
                  sbid = (roundrobin++ ^ hash) % max_sbids;
               }
            }

            jay_inst_set_sbid(I, sbid);
         }

         if (edge->tokens_bitset[sbid] == NULL) {
            edge->tokens_bitset[sbid] = alloc_sbid_bitset(edge->ctx);
         } else {
            /* Dispose of the bitset's previous contents */
            jay_foreach_sbid_dep_type(type) {
               uint32_t mask =
                  type == DST ? (busy_dst | sync_dst) : (busy_src | sync_src);
               if (mask & BITFIELD_BIT(sbid)) {
                  __bitset_zero(bitset_for(edge, sbid, type), edge->ctx->words);
               }
            }
         }

         /* SBID.set implies SBID.dst (which implies SBID.src), so elide */
         sync_dst &= ~BITFIELD_BIT(sbid);
         sync_src &= ~BITFIELD_BIT(sbid);
         busy_dst |= BITFIELD_BIT(sbid);
         busy_src |= BITFIELD_BIT(sbid);

         struct key dst = def_to_sbid_key(func, I, I->dst);
         BITSET_SET_COUNT(bitset_for(edge, sbid, DST), dst.base, dst.width);

         jay_foreach_src(I, s) {
            struct key src = def_to_sbid_key(func, I, I->src[s]);
            BITSET_SET_COUNT(bitset_for(edge, sbid, SRC), src.base, src.width);
         }

         /* Barriers are non-EOT gateway messages. Insert the needed SYNC */
         if (commit &&
             I->op == JAY_OPCODE_SEND &&
             jay_send_sfid(I) == GEN_SFID_MESSAGE_GATEWAY) {
            b.cursor = jay_after_inst(I);
            jay_SYNC(&b, jay_null(), TGL_SYNC_BAR);
         }
      } else if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         sync_dst |= busy_dst;
         sync_src |= busy_src & ~busy_dst;
         busy_dst = 0;
         busy_src = 0;
      }

      /* Dispose of the bitsets for any synced sbids */
      jay_foreach_sbid_dep_type(type) {
         u_foreach_bit(sbid, sync_dst | (type == SRC ? sync_src : 0)) {
            __bitset_zero(bitset_for(edge, sbid, type), edge->ctx->words);
         }
      }

      u_foreach_bit(sbid, (sync_src | sync_dst) & ~busy_dst) {
         release_sbid_bitset(edge->ctx, &edge->tokens_bitset[sbid]);
      }

      if (!commit)
         continue;

      b.cursor = jay_before_inst(I);
      assert(((sync_dst & sync_src) == 0) && "by construction");

      sync_sbids(&b, sync_dst, GEN_SBID_DST);
      sync_sbids(&b, sync_src, GEN_SBID_SRC);

      if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         /* Lowered above into a sync, but removed late to keep the cursor */
         jay_remove_instruction(I);
      }
   }

   edge->tokens_busy[SRC] = busy_src;
   edge->tokens_busy[DST] = busy_dst;
   validate_edge(edge);
}

/**
 * Regdist scoreboarding
 *
 * Register access is tracked per pipe, with 0 (NONE) having data on the writer
 * packed into a u32 with the following macros.
 */
#define make_writer(pipe, ip) (((uint32_t) ip << 3) | (uint32_t) (pipe))
#define writer_ip(writer)     (writer >> 3)
#define writer_pipe(writer)   (gen_pipe)(writer & BITFIELD_MASK(3))

#define GEN_NUM_PIPES (GEN_PIPE_ALL)
typedef uint32_t u32_per_pipe[GEN_NUM_PIPES];

struct swsb_regdist_state {
   uint32_t nr_keys;
   unsigned ip[GEN_NUM_PIPES];
   unsigned last_shape[GEN_NUM_PIPES];

   /* finished_ip[X / GEN_NUM_PIPES + SBID][Y] = ip means from the perspective
    * of pipe X or send SBID X, ip on pipe Y has already been waited on.
    */
   unsigned finished_ip[GEN_NUM_PIPES + NUM_TOKENS][GEN_NUM_PIPES];
   u32_per_pipe *access;

   jay_inst *last_sync;
};

/*
 * Return the maximum ALU distance to consider. Anything further is guaranteed
 * to have already written its result by the time we issue. These values are not
 * in the bspec but are #define'd in IGC as SWSB_MAX_*_DEPENDENCE_DISTANCE.
 *
 * Confusingly, IGC also defines SWSB_MAX_ALU_DEPENDENCE_DISTANCE_VALUE as 7.
 * There is a discrepency between what the hardware does and what we can encode.
 * Any writes from 11 instructions ago are guaranteed to have landed, whereas if
 * you need to sync, you can only sync with something up to 7 instructions ago
 * (and implicitly, everything in-order before that).
 *
 * These are conservative values. Some archeology suggests the real values may
 * be lower on some platforms but for now we match IGC to be safe.
 */
static inline unsigned
max_dependence(gen_pipe pipe)
{
   return pipe == GEN_PIPE_SCALAR ? 2 :
          pipe == GEN_PIPE_MATH   ? 18 :
          pipe == GEN_PIPE_LONG   ? 15 :
                                    11;
}

static void
depend_on_writer(struct swsb_regdist_state *state,
                 struct key r,
                 unsigned *dep,
                 gen_pipe exec,
                 bool except_exec)
{
   for (unsigned i = 0; i < r.width; ++i) {
      assert(r.base + i < state->nr_keys);
      uint32_t w = state->access[r.base + i][0];
      gen_pipe write = writer_pipe(w);

      /* We omit write-after-{read,write} dependencies (except_exec) within a
       * single execution pipe, since each pipe is internally in-order. We also
       * omit dependencies on the same pipe that are too far to be relevant.
       */
      if (write != exec ||
          (!except_exec &&
           writer_ip(w) + max_dependence(exec) > state->ip[write])) {

         dep[write] = MAX2(dep[write], writer_ip(w));
      }
   }
}

#define jay_foreach_pipe(pipe)                                                 \
   for (unsigned pipe = 1; pipe < GEN_NUM_PIPES; ++pipe)

static void
lower_regdist(jay_function *func, jay_inst *I, struct swsb_regdist_state *ctx)
{
   if (I->op == JAY_OPCODE_SYNC) {
      ctx->last_sync = I;
      uint32_t sbid_mask = 0;
      if (jay_sync_op(I) == TGL_SYNC_NOP) {
         /* The SYNC.nops added by this function that are RegDist-only, are
          * added *before* the instruction so are not seen here.
          */
         assert(I->dep.mode != GEN_SBID_NULL);
         sbid_mask = BITFIELD_BIT(I->dep.sbid);
      } else if (jay_sync_op(I) == TGL_SYNC_ALLRD ||
                 jay_sync_op(I) == TGL_SYNC_ALLWR) {
         sbid_mask = jay_as_uint(I->src[0]);
      }

      /* Syncs execute on all pipes, so any regdist that the synced SEND waited
       * on gets cleared for all pipes. This reduces annotations.
       */
      u_foreach_bit(sbid, sbid_mask) {
         jay_foreach_pipe(p) {
            jay_foreach_pipe(q) {
               ctx->finished_ip[p][q] =
                  MAX2(ctx->finished_ip[p][q],
                       ctx->finished_ip[GEN_NUM_PIPES + sbid][q]);
            }
         }
      }

      return;
   }

   gen_pipe exec_pipe = jay_inst_exec_pipe(func->shader->devinfo, I);
   unsigned dep[GEN_NUM_PIPES] = { 0 };
   jay_def dsts[3] = { I->dst, I->cond_flag };

   /* MUL_32 is a macro implicitly clobbering acc0/acc1 */
   if (I->op == JAY_OPCODE_MUL_32) {
      unsigned n = func->shader->dispatch_width < 32 ? 2 : 1;
      dsts[2] = jay_bare_regs(ACCUM, 0, n);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(dsts); ++i) {
      struct key r = def_to_regdist_key(func, I, dsts[i]);
      depend_on_writer(ctx, r, dep, exec_pipe, true /* except_pipe */);

      for (unsigned i = 0; i < r.width; ++i) {
         jay_foreach_pipe(p) {
            if (p != exec_pipe) {
               dep[p] = MAX2(dep[p], ctx->access[r.base + i][p]);
            }
         }
      }
   }

   /* Read-after-write. The hardware scoreboards accumulators within a pipe, so
    * we set except_pipe for that to omit those annotations. The hardware does
    * *not* scoreboard accumulators across pipes so we can't just ignore
    * accumulators when scoreboarding. For example, the I@1 annotation is
    * required in the following code:
    *
    * (16)        mul.s32 acc0, g26, g24<16,8,2>:u16                  │
    * (32)        mad.f32 acc0, u8.6, u8.8, g20                       │ I@1
    */
   jay_foreach_src(I, s) {
      depend_on_writer(ctx, def_to_regdist_key(func, I, I->src[s]), dep,
                       exec_pipe, I->src[s].file == ACCUM /* except_pipe */);
   }

   /* If dependency P implies dependency Q, drop dependency Q to avoid
    * unnecessary annotations.
    */
   jay_foreach_pipe(p) {
      if (dep[p]) {
         jay_foreach_pipe(q) {
            if (p != q && dep[q] && ctx->finished_ip[p][q] >= dep[q]) {
               dep[q] = 0;
            }
         }
      }
   }

   uint32_t wait_pipes = 0;
   unsigned min_delta = 7;

   jay_foreach_pipe(p) {
      if (dep[p] && (exec_pipe == GEN_PIPE_NONE ||
                     dep[p] > ctx->finished_ip[exec_pipe][p])) {

         min_delta = MIN2(min_delta, ctx->ip[p] - dep[p] + 1);
         wait_pipes |= BITFIELD_BIT(p);
      }
   }

   /* Unordered instructions are modelled as a pipe per SBID for
    * finished_ip purposes.
    */
   unsigned generalized_pipe = exec_pipe;
   if (jay_inst_is_unordered(I)) {
      generalized_pipe = GEN_NUM_PIPES + jay_inst_sbid(I);
   }

   /* We'll wait on the unioned dependency. Update the tracking for that. */
   u_foreach_bit(p, wait_pipes) {
      ctx->finished_ip[generalized_pipe][p] = ctx->ip[p] + 1 - min_delta;
   }

   uint32_t last_pipe = util_logbase2(wait_pipes);
   bool single_wait = wait_pipes == BITFIELD_BIT(last_pipe);

   /* If we're SIMD split the same way as our dependency, we can relax the
    * dependency to have each half wait in parallel. We could do even better
    * with more tracking but this should be good enough for now.
    */
   unsigned simd_split = jay_simd_split(func->shader, I);
   unsigned shape = ((simd_split << 2) | jay_macro_length(I)) + 1;
   bool same_shape = ctx->last_shape[last_pipe] == shape;

   if (simd_split && same_shape && single_wait && min_delta == 1) {
      min_delta += ((1 << simd_split) - 1) * jay_macro_length(I);
      I->replicate_dep = true;
      I->decrement_dep = last_pipe != exec_pipe;
   }

   bool has_sbid = jay_inst_has_sbid(I);
   I->dep = (gen_swsb) {
      .sbid = has_sbid ? jay_inst_sbid(I) : 0,
      .mode = has_sbid ? GEN_SBID_SET : GEN_SBID_NULL,
      .regdist = wait_pipes ? min_delta : 0,
      .pipe = single_wait && (!has_sbid ||
                              last_pipe == GEN_PIPE_FLOAT ||
                              last_pipe == GEN_PIPE_INT) ?
                 last_pipe :
              wait_pipes ? GEN_PIPE_ALL :
                           GEN_PIPE_NONE,
   };

   /* DPAS can only represent in-order dependency for its inferred pipe,
    * so if it depends on something else, add an extra SYNC.nop for that.
    */
   if (I->op == JAY_OPCODE_DPAS &&
       wait_pipes &&
       (!single_wait ||
        last_pipe != jay_inferred_sync_pipe(func->shader->devinfo, I))) {
      assert(I->dep.regdist > 0);
      jay_builder b = jay_init_builder(func, jay_before_inst(I));

      jay_inst *sync = jay_SYNC(&b, jay_null(), TGL_SYNC_NOP);
      sync->dep.regdist = I->dep.regdist;
      sync->dep.pipe = I->dep.pipe;

      I->dep.regdist = 0;
      I->dep.pipe = GEN_PIPE_NONE;
   }

   /* Fold the immediate preceding SYNC.nop into this instruction, allowing
    * us to wait on both ALU and a SBID in the same annotation. We cannot do
    * this safely in the presence of predication or SIMD splitting that could
    * cause any part of the instruction to get shot down, skipping the sync
    * for future instructions (at least not without more tricky logic).
    */
   if (ctx->last_sync &&
       jay_sync_op(ctx->last_sync) == TGL_SYNC_NOP &&
       I->dep.mode == GEN_SBID_NULL &&
       !I->predication &&
       !jay_simd_split(func->shader, I) &&
       (I->dep.regdist == 0 ||
        jay_inferred_sync_pipe(func->shader->devinfo, I) == I->dep.pipe)) {

      assert(ctx->last_sync->dep.regdist == 0);
      assert(ctx->last_sync->dep.pipe == GEN_PIPE_NONE);

      I->dep.mode = ctx->last_sync->dep.mode;
      I->dep.sbid = ctx->last_sync->dep.sbid;

      jay_remove_instruction(ctx->last_sync);
   }

   if (exec_pipe != GEN_PIPE_NONE) {
      /* Advance the IP by the number of physical instructions emitted */
      ctx->ip[exec_pipe] +=
         jay_macro_length(I) << jay_simd_split(func->shader, I);

      uint32_t now = make_writer(exec_pipe, ctx->ip[exec_pipe]);

      for (unsigned i = 0; i < ARRAY_SIZE(dsts); ++i) {
         struct key r = def_to_regdist_key(func, I, dsts[i]);

         for (unsigned i = 0; i < r.width; ++i) {
            ctx->access[r.base + i][0] = now;
         }
      }

      jay_foreach_src(I, s) {
         struct key r = def_to_regdist_key(func, I, I->src[s]);
         for (unsigned i = 0; i < r.width; ++i) {
            ctx->access[r.base + i][exec_pipe] = ctx->ip[exec_pipe];
         }
      }

      ctx->last_shape[exec_pipe] = shape;
   }

   ctx->last_sync = NULL;
}

/*
 * Trivial scoreboard lowering pass for debugging use. Stalls after every
 * instruction and assigns SBID zero to all messages.
 */
void
jay_lower_scoreboard_trivial(jay_shader *shader)
{
   jay_foreach_inst_in_shader_safe(shader, func, I) {
      if (jay_inst_has_sbid(I)) {
         /* DPAS can't have an A@1, so insert an extra SYNC.nop. */
         jay_builder before = jay_init_builder(func, jay_before_inst(I));
         jay_SYNC(&before, jay_null(), TGL_SYNC_NOP)->dep = gen_swsb_regdist(1);
         I->dep = gen_swsb_sbid(GEN_SBID_SET, 0);

         jay_builder b = jay_init_builder(func, jay_after_inst(I));
         sync_sbids(&b, BITFIELD_BIT(0), GEN_SBID_DST);

         /* Barriers are non-EOT gateway messages. Insert the needed SYNC */
         if (I->op == JAY_OPCODE_SEND &&
             jay_send_sfid(I) == GEN_SFID_MESSAGE_GATEWAY) {
            b.cursor = jay_after_inst(I);
            jay_SYNC(&b, jay_null(), TGL_SYNC_BAR);
         }
      } else if (I->op == JAY_OPCODE_SCHEDULE_BARRIER) {
         jay_remove_instruction(I);
      } else {
         I->dep = gen_swsb_regdist(1);
      }
   }
}

void
jay_lower_scoreboard(jay_shader *shader)
{
   unsigned accums = 4;
   uint32_t nr_regdist_keys =
      shader->num_regs[GPR] + shader->num_regs[UGPR] + accums;
   u32_per_pipe *regdists = malloc(sizeof(*regdists) * nr_regdist_keys);

   unsigned max_blocks = 0;
   jay_foreach_function(shader, f)
      max_blocks = MAX2(max_blocks, f->num_blocks);

   uint32_t nr_sbid_keys =
      shader->num_regs[GPR] +
      DIV_ROUND_UP(shader->num_regs[UGPR], jay_ugpr_per_grf(shader)) +
      accums;

   unsigned max_sbids = intel_device_info_max_sbids(shader->devinfo);

   struct swsb_sbid_state sbid_state;
   init_sbid_state(&sbid_state, max_blocks, nr_sbid_keys, max_sbids);

   unsigned dirty_blocks = 0;
   jay_foreach_function(shader, f) {
      memset(regdists, 0, sizeof(*regdists) * nr_regdist_keys);
      clear_sbid_state(&sbid_state, dirty_blocks);
      dirty_blocks = f->num_blocks;

      /* Thanks to programs always using structured control flow, and blocks
       * being iterated in program order, global SBID scoreboarding can be
       * accomplished in just two passes. This will no longer work if we ever
       * start handling arbitrary GOTOs.
       */
      for (unsigned commit = 0; commit <= 1; ++commit) {
         jay_foreach_block(f, block) {
            struct swsb_sbid_edge *edge = &sbid_state.edges[block->index];
            merge_sbid_edges_of_preds(&sbid_state, block, edge);
            lower_sbid_local(f, block, edge, commit);
         }
      }

      struct swsb_regdist_state regdist_state = { .nr_keys = nr_regdist_keys,
                                                  .access = regdists };

      /* RegDist scoreboarding is global but requires no dataflow analysis,
       * because taking a branch stalls all ALU pipelines. Therefore, it
       * suffices to propagate scoreboard state along fallthrough edges. We
       * implement that backwards: state is preserved (correctness), except we
       * clear regdists[] when entering blocks that are unreachable by falling
       * through from the previous source-order block and hence must be branch
       * targets coming in with a clear scoreboard. next[] tracks the
       * fallthrough block for the logical & physical CFGs respectively.
       */
      jay_block *next[UGPR + 1] = { NULL };

      jay_foreach_block(f, block) {
         /* Clear regdists[] for GPRs according to the logical CFG and for UGPRs
          * according to the physical CFG. This is a bit pedantic but it ensures
          * we keep the dependencies for UGPRs across halves of if-else.
          */
         for (unsigned f = GPR; f <= UGPR; f++) {
            if (!list_is_empty(&block->instructions) && next[f] != block) {
               memset(regdists + (f ? shader->num_regs[GPR] : 0), 0,
                      sizeof(regdists[0]) * shader->num_regs[f]);
            }

            next[f] = jay_successors(block, f)[0];
         }

         jay_foreach_inst_in_block_safe(block, I) {
            lower_regdist(f, I, &regdist_state);
         }
      }
   }

   free(regdists);
   ralloc_free(sbid_state.mem_ctx);
}
