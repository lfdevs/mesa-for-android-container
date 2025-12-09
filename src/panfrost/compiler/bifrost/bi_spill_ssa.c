/*
 * Copyright 2023-2024 Alyssa Rosenzweig
 * Copyright 2023-2024 Valve Corporation
 * Copyright 2022,2025 Collabora Ltd.
 * Copyright 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "util/u_qsort.h"
#include "bi_builder.h"
#include "bifrost_nir.h"
#include "compiler.h"

/* allow at least this many temporaries for spilling */
#define MIN_TEMPS_FOR_SPILL 4

/*
 * An implementation of "Register Spilling and Live-Range Splitting for SSA-Form
 * Programs" by Braun and Hack.
 */

/*
 * Next-use distances are logically in ℤ ∪ {∞}. Modeled as saturating uint32 and
 * referred to as dist_t.
 *
 * next_uses represents a next-use map. This is a sparse data structure mapping
 * variable names to next-use dist_t's. Variables with no later use (infinite
 * next-use distance) are not stored explicitly, making the time/space
 * requirements O(live variables). This is important for performance and memory
 * usage on big shaders with many blocks.
 *
 * For now, next_uses is backed by a Mesa hash table, but it could be optimized
 * to something more specialized in the future.
 */
#define DIST_INFINITY (UINT32_MAX)
typedef uint32_t dist_t;

static dist_t
dist_sum(dist_t A, dist_t B)
{
   return (A + B < A) ? DIST_INFINITY : (A + B);
}

struct next_uses {
   struct hash_table_u64 *ht;
};

static void
init_next_uses(struct next_uses *nu, void *memctx)
{
   nu->ht = _mesa_hash_table_u64_create(memctx);
}

static void
destroy_next_uses(struct next_uses *nu)
{
   _mesa_hash_table_u64_destroy(nu->ht);
}

static void
clear_next_uses(struct next_uses *nu)
{
   _mesa_hash_table_u64_clear(nu->ht);
}

static void
copy_next_uses(struct next_uses *nu, const struct next_uses *from)
{
   clear_next_uses(nu);

   hash_table_u64_foreach(from->ht, use) {
      _mesa_hash_table_u64_insert(nu->ht, use.key, use.data);
   }
}

static void
set_next_use(struct next_uses *nu, unsigned node, dist_t dist)
{
   if (dist == DIST_INFINITY) {
      _mesa_hash_table_u64_remove(nu->ht, node);
   } else {
      uintptr_t as_ptr = (uintptr_t)(dist + 1);
      assert(as_ptr != 0 && "non-NULL");

      _mesa_hash_table_u64_insert(nu->ht, node, (void *)as_ptr);
   }
}

static dist_t
search_next_uses(const struct next_uses *nu, unsigned node)
{
   void *ent = _mesa_hash_table_u64_search(nu->ht, node);
   if (!ent)
      return DIST_INFINITY;

   return ((uintptr_t)ent) - 1;
}

#define foreach_next_use(nu, node, dist)                                       \
   hash_table_u64_foreach((nu)->ht, use_)                                      \
      for (uint32_t _terminator = 1, node = use_.key,                          \
                    UNUSED dist = ((uintptr_t)use_.data) - 1;                  \
           _terminator != 0; _terminator = 0)

/*
 * Calculate the minimum of two next-use sets. Values absent from one of the
 * underlying sets are infinity so do not contribute to the minimum, instead
 * acting like a set union.
 */
static bool
minimum_next_uses(struct next_uses *nu, const struct next_uses *from)
{
   bool progress = false;

   foreach_next_use(from, node, from_dist) {
      dist_t nu_dist = search_next_uses(nu, node);

      if (from_dist < nu_dist) {
         set_next_use(nu, node, from_dist);
         progress = true;
      }
   }

   return progress;
}

static uint32_t
instr_cycles(const bi_instr *I)
{
   return 1;
}

struct spill_block {
   /* Set of values available in the register file at the end */
   unsigned W_exit[BI_MAX_REGS];
   unsigned nW_exit;

   unsigned W_entry[BI_MAX_REGS];
   unsigned nW_entry;

   /* Set of live-out spilled values at the end of the block */
   unsigned *S_exit;
   unsigned nS_exit;

   unsigned *S_entry;
   unsigned nS_entry;

   /* Estimate */
   uint32_t cycles;

   /* Next-use maps at the start/end of the block */
   struct next_uses next_use_in;
   struct next_uses next_use_out;
};

struct spill_ctx {
   void *memctx;
   bi_context *shader;
   bi_block *block;

   /* Set of values currently available in the register file */
   BITSET_WORD *W;

   /* |W| = Current register pressure */
   unsigned nW;

   /* Local IPs of next-use */
   dist_t *next_uses;

   /* Current local IP relative to the start of the block */
   uint32_t ip;

   /* Set of live values that have been spilled. Contrary to the paper, this
    * is not a subset of W: the definition in the paper is bogus.
    */
   BITSET_WORD *S;

   /* Widths of vectors */
   uint32_t *size;

   /* Mapping of rematerializable values to their definitions, or NULL for nodes
    * that are not materializable.
    */
   bi_instr **remat;

   /* Maximum register pressure allowed */
   unsigned k;

   /* Number of variables allocated */
   unsigned n_alloc;

   /* Information on blocks indexed in source order */
   struct spill_block *blocks;

   /* first FAU index for spilled registers */
   unsigned spill_base;

   /* Max index reserved for spilled indices */
   uint32_t spill_max;

   /* count of spilled bytes */
   uint32_t spill_bytes;

   /* mapping of registers to spill locations */
   uint32_t *spill_map;
   /* and the reverse */
   uint32_t *mem_map;
   /* used to mark if a location has been stored to already. */
   BITSET_WORD *spill_map_store;

   /* Instruction that defines an SSA value. */
   bi_instr **ssa_defs;

   /* Block that defines an SSA value. */
   bi_block **ssa_def_blocks;

   /* architecture */
   unsigned arch;
};

static inline struct spill_block *
spill_block(struct spill_ctx *ctx, bi_block *block)
{
   return &ctx->blocks[block->index];
}

/* Calculate the register demand of a node. This should be rounded up to
 * a power-of-two to match the equivalent calculations in RA.
 */
static inline unsigned
node_size(struct spill_ctx *ctx, unsigned node)
{
   return ctx->size[node];
}

/*
 * Map a control flow edge to a block. Assumes no critical edges.
 */
static bi_block *
bi_edge_to_block(bi_block *pred, bi_block *succ)
{
   /* End of predecessor is unique if there's a single successor */
   if (bi_num_successors(pred) == 1)
      return pred;

   /* The predecessor has multiple successors, meaning this is not the only
    * edge leaving the predecessor. Therefore, it is the only edge entering
    * the successor (otherwise the edge would be critical), so the start of
    * the successor is unique.
    */
   assert(bi_num_predecessors(succ) == 1 && "critical edge detected");
   return succ;
}

/*
 * Get a cursor to insert along a control flow edge: either at the start of the
 * successor or the end of the predecessor. This relies on the control flow
 * graph having no critical edges.
 */
static bi_cursor
bi_along_edge(bi_block *pred, bi_block *succ)
{
   bi_block *to = bi_edge_to_block(pred, succ);

   if (to == pred)
      return bi_after_block_logical(pred);
   else
      return bi_before_block(succ);
}

static bool bi_idx_is_memory(bi_index idx) {
//   return (idx.type == BI_INDEX_FAU);
   return idx.memory;
}

static bi_index
bi_index_as_mem(bi_index idx, struct spill_ctx *ctx)
{
   assert(idx.type == BI_INDEX_NORMAL);
   idx.type = BI_INDEX_FAU;
   unsigned val = idx.value;

   assert(val < ctx->spill_max);
   if (ctx->spill_map[val] == 0xFFFFFFFFU) {
      uint32_t remap = ctx->spill_bytes;
      ctx->spill_bytes += 4;
      ctx->spill_map[val] = remap;
      unsigned i = (remap - ctx->spill_base)/4;
      assert(i < ctx->spill_max);
      ctx->mem_map[i] = val;
   }
   idx.value = ctx->spill_map[val];
   idx.memory = true;
   return idx;
}

static unsigned
chase_mem_index(bi_index ref, struct spill_ctx *ctx)
{
   unsigned val = ref.value;
   if (bi_idx_is_memory(ref)) {
      unsigned i = (val - ctx->spill_base)/4;
      return ctx->mem_map[i];
   }
   return val;
}

static bi_index
reconstruct_index(struct spill_ctx *ctx, unsigned node)
{
   bi_index r = bi_get_index(node);
   /* do we need to reconstruct the swizzle here? */
   return r;
}

static bool
only_const_sources(bi_instr *I)
{
   bool res = true;
   for (uint32_t s = 0; s < I->nr_srcs && res; ++s) {
      bi_index src = I->src[s];
      enum bi_index_type t = src.type;
      res &= t == BI_INDEX_CONSTANT || t == BI_INDEX_FAU;
   }
   return res;
}

static bool
can_remat(bi_instr *I)
{
   switch (I->op) {
   case BI_OPCODE_IADD_IMM_I32:
   case BI_OPCODE_IADD_IMM_V2I16:
      return only_const_sources(I);
   case BI_OPCODE_LD_PKA_I8:
   case BI_OPCODE_LD_PKA_I16:
   case BI_OPCODE_LD_PKA_I24:
   case BI_OPCODE_LD_PKA_I32:
      /* TODO: Allow loads that write >1 reg. */
      return only_const_sources(I);
   case BI_OPCODE_MOV_I32:
      assert(!I->src[0].memory);
      assert(!I->dest[0].memory);
      assert(I->dest[0].type == BI_INDEX_NORMAL);
      enum bi_index_type t = I->src[0].type;
      return t == BI_INDEX_CONSTANT || t == BI_INDEX_FAU;
   default:
      return false;
   }
}

static bi_instr *
remat_to(bi_builder *b, bi_index dst, struct spill_ctx *ctx, unsigned node)
{
   assert(dst.type == BI_INDEX_NORMAL);
   assert(node < ctx->spill_max);
   bi_instr *I = ctx->remat[node];
   assert(can_remat(I));

   switch (I->op) {
   case BI_OPCODE_IADD_IMM_I32:
      return bi_iadd_imm_i32_to(b, dst, I->src[0], I->index);
   case BI_OPCODE_IADD_IMM_V2I16:
      return bi_iadd_imm_v2i16_to(b, dst, I->src[0], I->index);
   case BI_OPCODE_LD_PKA_I8:
      return bi_ld_pka_i8_to(b, dst, I->src[0], I->src[1]);
   case BI_OPCODE_LD_PKA_I16:
      return bi_ld_pka_i16_to(b, dst, I->src[0], I->src[1]);
   case BI_OPCODE_LD_PKA_I24:
      return bi_ld_pka_i24_to(b, dst, I->src[0], I->src[1]);
   case BI_OPCODE_LD_PKA_I32:
      return bi_ld_pka_i32_to(b, dst, I->src[0], I->src[1]);
   case BI_OPCODE_MOV_I32:
      assert(I->src[0].type == BI_INDEX_CONSTANT ||
             I->src[0].type == BI_INDEX_FAU);
      return bi_mov_i32_to(b, dst, I->src[0]);
   default:
      UNREACHABLE("invalid remat");
   }
}

static bi_cursor
choose_spill_position(struct spill_ctx *ctx, unsigned node, bi_cursor fallback)
{
   bi_instr *producer = ctx->ssa_defs[node];
   bi_block *block = ctx->ssa_def_blocks[node];

   assert(producer);
   assert(block);

   if (ctx->block == block)
      return fallback;

   /* Don't insert spills in the middle of the PHI block. This breaks the
    * assumption that phis are together at the start of each block. */
   if (producer->op == BI_OPCODE_PHI) {
      return bi_after_block_logical(block);
   } else {
      return bi_after_instr(producer);
   }
}

static void
insert_spill(bi_builder *b, struct spill_ctx *ctx, unsigned node)
{
   assert(node < ctx->spill_max);
   if (!ctx->remat[node] && !BITSET_TEST(ctx->spill_map_store, node)) {
      bi_index idx = reconstruct_index(ctx, node);
      bi_index mem = bi_index_as_mem(idx, ctx);
      unsigned bits = 32;

      bi_store_tl(b, bits, idx, mem.value);

      b->shader->spills++;
      /* We only need the extra registers reserved if we actually spilled
       * instead of just remat.
       */
      b->shader->has_spill_pcopy_reserved = true;

      BITSET_SET(ctx->spill_map_store, node);
   }
}

static void
insert_reload(struct spill_ctx *ctx, bi_block *block, bi_cursor cursor,
              unsigned node)
{
   bi_builder b = bi_init_builder(ctx->shader, cursor);
   bi_index idx = reconstruct_index(ctx, node);

   /* Reloading breaks SSA, but we're leaving SSA anyway */
   assert(node < ctx->spill_max);
   if (ctx->remat[node]) {
      remat_to(&b, idx, ctx, node);
   } else {
      bi_index mem = bi_index_as_mem(idx, ctx);
      unsigned bits = 32;
      bi_load_tl(&b, bits, idx, mem.value);
      b.shader->fills++;
   }
}

/* Insert into the register file */
static void
insert_W(struct spill_ctx *ctx, unsigned v)
{
   assert(v < ctx->n_alloc);
   assert(!BITSET_TEST(ctx->W, v));

   BITSET_SET(ctx->W, v);
   ctx->nW += node_size(ctx, v);
}

/* Remove from the register file */
static void
remove_W(struct spill_ctx *ctx, unsigned v)
{
   assert(v < ctx->n_alloc);
   assert(BITSET_TEST(ctx->W, v));

   BITSET_CLEAR(ctx->W, v);
   ctx->nW -= node_size(ctx, v);
}

static void
remove_W_if_present(struct spill_ctx *ctx, unsigned v)
{
   assert(v < ctx->n_alloc);

   if (BITSET_TEST(ctx->W, v))
      remove_W(ctx, v);
}

#define bi_worklist_init(ctx, w)        u_worklist_init(w, ctx->num_blocks, ctx)
#define bi_worklist_push_head(w, block) u_worklist_push_head(w, block, index)
#define bi_worklist_push_tail(w, block) u_worklist_push_tail(w, block, index)
#define bi_worklist_peek_head(w)        u_worklist_peek_head(w, bi_block, index)
#define bi_worklist_pop_head(w)         u_worklist_pop_head(w, bi_block, index)
#define bi_worklist_peek_tail(w)        u_worklist_peek_tail(w, bi_block, index)
#define bi_worklist_pop_tail(w)         u_worklist_pop_tail(w, bi_block, index)

struct candidate {
   unsigned node;
   dist_t dist;
};

static int
cmp_dist(const void *left_, const void *right_, void *ctx_)
{
   struct spill_ctx *ctx = ctx_;
   const struct candidate *left = left_;
   const struct candidate *right = right_;
   unsigned ldist = left->dist;
   unsigned rdist = right->dist;
   /* We assume that rematerializing - even before every instruction - is
    * cheaper than spilling. As long as one of the nodes is rematerializable
    * (with distance > 0), we choose it over spilling. Within a class of nodes
    * (rematerializable or not), compare by next-use-distance.
    */
   assert(left->node < ctx->n_alloc);
   assert(right->node < ctx->n_alloc);
   bool remat_left = ctx->remat[left->node] != NULL && ldist > 0;
   bool remat_right = ctx->remat[right->node] != NULL && rdist > 0;

   if (remat_left != remat_right)
      return remat_left ? 1 : -1;
   else
      return (ldist > rdist) - (ldist < rdist);
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
insert_coupling_code(struct spill_ctx *ctx, bi_block *pred, bi_block *succ)
{
   struct spill_block *sp = spill_block(ctx, pred);
   struct spill_block *ss = spill_block(ctx, succ);

   bi_foreach_phi_in_block(succ, I) {
      if (!bi_idx_is_memory(I->dest[0]))
         continue;

      bi_builder b =
         bi_init_builder(ctx->shader, bi_before_function(ctx->shader));

      unsigned s = bi_predecessor_index(succ, pred);

      /* Copy immediate/uniform phi sources to memory variables at the start of
       * the program, where pressure is zero and hence the copy is legal.
       */
      if (I->src[s].type != BI_INDEX_NORMAL && I->src[s].type != BI_INDEX_FAU) {
         assert(I->src[s].type == BI_INDEX_CONSTANT ||
                I->src[s].type == BI_INDEX_REGISTER);

         bi_index gpr = bi_temp(ctx->shader);
         unsigned bits = 32;

         assert(gpr.type == BI_INDEX_NORMAL);
         if (ctx->arch >= 9 && I->src[s].type == BI_INDEX_CONSTANT) {
            /* MOV of immediate needs lowering on Valhall */
            bi_index zero = bi_fau(BIR_FAU_IMMEDIATE | 0, false);
            bi_iadd_imm_i32_to(&b, gpr, zero, I->src[s].value);
         } else
            bi_mov_i32_to(&b, gpr, I->src[s]);
         bi_index mem = bi_index_as_mem(gpr, ctx);
         bi_store_tl(&b, bits, gpr, mem.value);
         I->src[s] = mem;
         continue;
      }

      bool spilled = false;
      for (unsigned i = 0; i < sp->nS_exit; ++i) {
         if (sp->S_exit[i] == I->src[s].value) {
            spilled = true;
            break;
         }
      }

      if (!spilled) {
         bi_cursor c = choose_spill_position(ctx, I->src[s].value,
                                             bi_after_block_logical(pred));
         bi_builder b = bi_init_builder(ctx->shader, c);

         insert_spill(&b, ctx, I->src[s].value);
      }

      if (ctx->remat[I->src[s].value]) {
         unsigned node = I->src[s].value;
         bi_index idx = reconstruct_index(ctx, node);
         bi_index tmp = bi_temp(ctx->shader);
         unsigned bits = 32;

         remat_to(&b, tmp, ctx, node);
         bi_store_tl(&b, bits, tmp, bi_index_as_mem(idx, ctx).value);
      }

      /* Use the spilled version */
      I->src[s] = bi_index_as_mem(I->src[s], ctx);
   }

   /* Anything assumed to be spilled at the start of succ must be spilled along
    * all edges.
    */
   for (unsigned i = 0; i < ss->nS_entry; ++i) {
      unsigned v = ss->S_entry[i];

      bool spilled = false;
      for (unsigned j = 0; j < sp->nS_exit; ++j) {
         if (sp->S_exit[j] == v) {
            spilled = true;
            break;
         }
      }

      /* We handle spilling phi destinations separately */
      bi_foreach_phi_in_block(succ, phi) {
         if (chase_mem_index(phi->dest[0], ctx) == v) {
            spilled = true;
            break;
         }
      }

      if (spilled)
         continue;

      bi_cursor c = choose_spill_position(ctx, v, bi_along_edge(pred, succ));
      bi_builder b = bi_init_builder(ctx->shader, c);
      insert_spill(&b, ctx, v);
   }

   /* Variables in W at the start of succ must be defined along the edge. */
   for (unsigned i = 0; i < ss->nW_entry; ++i) {
      unsigned node = ss->W_entry[i];
      bool defined = false;

      /* Variables live at the end of the predecessor are live along the edge */
      for (unsigned j = 0; j < sp->nW_exit; ++j) {
         if (sp->W_exit[j] == node) {
            defined = true;
            break;
         }
      }

      /* Phis are defined along the edge */
      bi_foreach_phi_in_block(succ, phi) {
         if (phi->dest[0].value == node) {
            defined = true;
            break;
         }
      }

      if (defined)
         continue;

      /* Otherwise, inserting a reload defines the variable along the edge */
      bi_block *reload_block = bi_edge_to_block(pred, succ);
      insert_reload(ctx, reload_block, bi_along_edge(pred, succ), node);
   }

   bi_foreach_phi_in_block(succ, I) {
      if (bi_idx_is_memory(I->dest[0]))
         continue;

      unsigned s = bi_predecessor_index(succ, pred);

      /* Treat immediate/uniform phi sources as registers for pressure
       * accounting and phi lowering purposes. Parallel copy lowering can handle
       * a copy from a immediate/uniform to a register, but not from an
       * immediate/uniform directly to memory.
       */
      if (I->src[s].type != BI_INDEX_NORMAL && !I->src[s].memory) {
         assert(I->src[s].type == BI_INDEX_CONSTANT ||
                I->src[s].type == BI_INDEX_REGISTER ||
                I->src[s].type == BI_INDEX_FAU
            );

         continue;
      }

      bool live = false;
      for (unsigned i = 0; i < sp->nW_exit; ++i) {
         if (sp->W_exit[i] == I->src[s].value) {
            live = true;
            break;
         }
      }

      /* Fill the phi source in the predecessor */
      if (!live) {
         bi_block *reload_block = bi_edge_to_block(pred, succ);
         insert_reload(ctx, reload_block, bi_along_edge(pred, succ),
                       I->src[s].value);
      }

      /* Leave as-is for the GPR version */
      assert(!bi_idx_is_memory(I->src[s]));
   }
}

/*
 * Produce an array of next-use IPs relative to the start of the block. This is
 * an array of dist_t scalars, representing the next-use IP of each SSA dest
 * (right-to-left) and SSA source (left-to-right) of each instruction in the
 * block (bottom-to-top). Its size equals the # of SSA sources in the block.
 */
static ATTRIBUTE_NOINLINE void
calculate_local_next_use(struct spill_ctx *ctx, struct util_dynarray *out)
{
   struct spill_block *sb = spill_block(ctx, ctx->block);
   unsigned ip = sb->cycles;

   *out = UTIL_DYNARRAY_INIT;

   struct next_uses nu;
   init_next_uses(&nu, NULL);

   foreach_next_use(&sb->next_use_out, i, dist) {
      set_next_use(&nu, i, dist_sum(ip, dist));
   }

   bi_foreach_instr_in_block_rev(ctx->block, I) {
      ip -= instr_cycles(I);

      if (I->op != BI_OPCODE_PHI) {
         bi_foreach_ssa_dest_rev(I, d) {
            unsigned v = I->dest[d].value;
            dist_t next_dist = search_next_uses(&nu, v);
            util_dynarray_append(out, next_dist);
         }

         bi_foreach_ssa_src(I, s) {
            unsigned v = I->src[s].value;
            dist_t next_dist = search_next_uses(&nu, v);
            util_dynarray_append(out, next_dist);
            assert((next_dist == DIST_INFINITY) == I->src[s].kill_ssa);
            set_next_use(&nu, v, ip);
         }
      }
   }

   assert(ip == 0 && "cycle counting is consistent");
   destroy_next_uses(&nu);
}

/*
 * Let I_B be the set of live-in variables plus the set of variables defined
 * by phis. Then W_entry will contain variables
 * - I_B & <variables used in the loop>
 * - I_B & <variables live-through the loop> (if there is space left)
 */
static ATTRIBUTE_NOINLINE void
compute_w_entry_loop_header(struct spill_ctx *ctx)
{
   bi_block *block = ctx->block;
   struct spill_block *sb = spill_block(ctx, block);

   const uint32_t flags_len = ctx->n_alloc;
   bool *flag_mem = calloc(2 * flags_len, sizeof(bool));
   bool *alive = flag_mem;
   bool *used_in_loop = flag_mem + flags_len;

   /* alive := live-in + defined by phis */
   uint32_t i = 0;
   BITSET_FOREACH_SET(i, ctx->block->ssa_live_in, ctx->n_alloc) {
      alive[i] = true;
   }
   bi_foreach_phi_in_block(ctx->block, phi)
   {
      alive[phi->dest[0].value] = true;
   }

   /* Start with candidates := { v : v ∈ alive and used_in_loop(v) }. */
   struct candidate *candidates =
      calloc(ctx->n_alloc, sizeof(struct candidate));
   uint32_t n_ca = 0;

   uint32_t max_loop_pressure = 0;
   BITSET_WORD *loop_block = BITSET_RZALLOC(NULL, ctx->shader->num_blocks);
   bi_find_loop_blocks(ctx->shader, ctx->block, loop_block);

   bi_foreach_block(ctx->shader, block) {
      if (BITSET_TEST(loop_block, block->index)) {
         bi_foreach_instr_in_block(block, I) {
            max_loop_pressure = MAX2(max_loop_pressure, block->ssa_max_live);

            bi_foreach_src(I, s) {
               const uint32_t v = I->src[s].value;
               const bool is_reg = I->src[s].type == BI_INDEX_NORMAL;

               /* Only add live register values, and only add them once. */
               if (!is_reg || !alive[v] || used_in_loop[v])
                  continue;

               const dist_t d = search_next_uses(&sb->next_use_in, v);
               candidates[n_ca++] = (struct candidate){.node = v, .dist = d};
               used_in_loop[v] = true;
            }
         }
      }
   }

   ralloc_free(loop_block);

   /* Sort by next-use distance. */
   util_qsort_r(candidates, n_ca, sizeof(struct candidate), cmp_dist, ctx);

   const uint32_t n_ca_loop = n_ca;

   /* Find live-through values in case we want to add any. */
   if (n_ca < ctx->k) {
      for (i = 0; i < ctx->n_alloc; ++i) {
         const bool live_through = alive[i] && !used_in_loop[i];
         if (live_through) {
            const dist_t d = search_next_uses(&sb->next_use_in, i);
            candidates[n_ca++] = (struct candidate){.node = i, .dist = d};
         }
      }
   }
   const uint32_t n_lt = n_ca - n_ca_loop;

   /* Sort live-through variables by next-use distance. */
   util_qsort_r(candidates + n_ca_loop, n_lt, sizeof(struct candidate),
                cmp_dist, ctx);

   assert(max_loop_pressure >= n_lt);
   /* If the pressure caused by vars inside the loop t is < k, we have space
    * for more variables to put in W_entry. */
   const uint32_t t = max_loop_pressure - n_lt;
   if (t < ctx->k)
      n_ca = CLAMP(n_ca_loop + (ctx->k - t), 0, n_ca);
   else
      n_ca = n_ca_loop;

   /* Take as much as we can. */
   for (unsigned i = 0; i < n_ca; ++i) {
      unsigned node = candidates[i].node;
      unsigned comps = node_size(ctx, node);

      if ((ctx->nW + comps) <= ctx->k) {
         insert_W(ctx, node);
         sb->W_entry[sb->nW_entry++] = node;
      }
   }

   assert(ctx->nW <= ctx->k && "invariant");

   free(candidates);
   free(flag_mem);
}

/*
 * The W_entry will contain variables that are W_exit in
 * - all predecessors
 * - some predecessors, sorted by next-use distance
 */
static ATTRIBUTE_NOINLINE void
compute_w_entry_usual(struct spill_ctx *ctx)
{
   bi_block *block = ctx->block;
   struct spill_block *sb = spill_block(ctx, block);

   unsigned *freq = calloc(ctx->n_alloc, sizeof(unsigned));

   /* Record what's written at the end of each predecessor */
   bi_foreach_predecessor(ctx->block, P) {
      struct spill_block *sp = spill_block(ctx, *P);

      for (unsigned i = 0; i < sp->nW_exit; ++i) {
         unsigned v = sp->W_exit[i];
         freq[v]++;
      }
   }

   struct candidate *candidates = calloc(ctx->n_alloc, sizeof(struct candidate));
   unsigned j = 0;

   /* Variables that are in all predecessors are assumed in W_entry. Phis and
    * variables in some predecessors are scored by next-use.
    */
   foreach_next_use(&sb->next_use_in, i, dist) {
      if (freq[i] == bi_num_predecessors(ctx->block)) {
         insert_W(ctx, i);
      } else if (freq[i]) {
         candidates[j++] = (struct candidate){.node = i, .dist = dist};
      }
   }

   bi_foreach_phi_in_block(ctx->block, I) {
      bool all_found = true;

      bi_foreach_predecessor(ctx->block, pred) {
         struct spill_block *sp = spill_block(ctx, *pred);
         bool found = false;

         bi_index src = I->src[bi_predecessor_index(ctx->block, *pred)];
         if (src.type != BI_INDEX_NORMAL)
            continue;

         unsigned v = src.value;
         for (unsigned i = 0; i < sp->nW_exit; ++i) {
            if (sp->W_exit[i] == v) {
               found = true;
               break;
            }
         }

         all_found &= found;
      }

      /* Heuristic: if any phi source is spilled, spill the whole phi. This is
       * suboptimal, but it massively reduces pointless fill/spill chains with
       * massive phi webs.
       */
      if (!all_found)
         continue;

      candidates[j++] = (struct candidate){
         .node = I->dest[0].value,
         .dist = search_next_uses(&sb->next_use_in, I->dest[0].value),
      };
   }

   /* Sort by next-use distance */
   util_qsort_r(candidates, j, sizeof(struct candidate), cmp_dist, ctx);

   /* Take as much as we can */
   for (unsigned i = 0; i < j; ++i) {
      unsigned node = candidates[i].node;
      unsigned comps = node_size(ctx, node);

      if ((ctx->nW + comps) <= ctx->k) {
         insert_W(ctx, node);
         sb->W_entry[sb->nW_entry++] = node;
      }
   }

   assert(ctx->nW <= ctx->k && "invariant");

   free(freq);
   free(candidates);
}

/*
 * Compute W_entry for a block. Section 4.2 in the paper.
 */
static ATTRIBUTE_NOINLINE void
compute_w_entry(struct spill_ctx *ctx)
{
   bi_block *block = ctx->block;

   /* Nothing to do for start blocks */
   if (bi_num_predecessors(block) == 0)
      return;

   /* Loop headers have a different heuristic */
   if (block->loop_header) {
      compute_w_entry_loop_header(ctx);
   } else {
      compute_w_entry_usual(ctx);
   }
}

/*
 * We initialize S with the union of S at the exit of (forward edge)
 * predecessors and the complement of W, intersected with the live-in set. The
 * former propagates S forward. The latter ensures we spill along the edge when
 * a live value is not selected for the entry W.
 */
static ATTRIBUTE_NOINLINE void
compute_s_entry(struct spill_ctx *ctx)
{
   unsigned v;

   bi_foreach_predecessor(ctx->block, pred) {
      struct spill_block *sp = spill_block(ctx, *pred);

      for (unsigned i = 0; i < sp->nS_exit; ++i) {
         v = sp->S_exit[i];

         if (BITSET_TEST(ctx->block->ssa_live_in, v))
            BITSET_SET(ctx->S, v);
      }
   }

   BITSET_FOREACH_SET(v, ctx->block->ssa_live_in, ctx->n_alloc) {
      if (!BITSET_TEST(ctx->W, v))
         BITSET_SET(ctx->S, v);
   }

   /* Copy ctx->S to S_entry for later look-ups with coupling code */
   struct spill_block *sb = spill_block(ctx, ctx->block);
   unsigned nS = __bitset_count(ctx->S, BITSET_WORDS(ctx->n_alloc));
   sb->S_entry = ralloc_array(ctx->memctx, unsigned, nS);

   int i;
   BITSET_FOREACH_SET(i, ctx->S, ctx->n_alloc)
      sb->S_entry[sb->nS_entry++] = i;

   assert(sb->nS_entry == nS);
}

static ATTRIBUTE_NOINLINE void
global_next_use_distances(bi_context *ctx, void *memctx,
                          struct spill_block *blocks)
{
   u_worklist worklist;
   u_worklist_init(&worklist, ctx->num_blocks, NULL);

   bi_foreach_block(ctx, block) {
      struct spill_block *sb = &blocks[block->index];

      init_next_uses(&sb->next_use_in, memctx);
      init_next_uses(&sb->next_use_out, memctx);

      bi_foreach_instr_in_block(block, I) {
         sb->cycles += instr_cycles(I);
      }

      bi_worklist_push_head(&worklist, block);
   }

   /* Definitions that have been seen */
   BITSET_WORD *defined =
      malloc(BITSET_WORDS(ctx->ssa_alloc) * sizeof(BITSET_WORD));

   struct next_uses dists;
   init_next_uses(&dists, NULL);

   /* Iterate the work list in reverse order since liveness is backwards */
   while (!u_worklist_is_empty(&worklist)) {
      bi_block *blk = bi_worklist_pop_head(&worklist);
      struct spill_block *sb = &blocks[blk->index];

      /* Definitions that have been seen */
      memset(defined, 0, BITSET_WORDS(ctx->ssa_alloc) * sizeof(BITSET_WORD));

      /* Initialize all distances to infinity */
      clear_next_uses(&dists);

      uint32_t cycle = 0;

      /* Calculate dists. Phis are handled separately. */
      bi_foreach_instr_in_block(blk, I) {
         if (I->op == BI_OPCODE_PHI) {
            cycle++;
            continue;
         }

         /* Record first use before def. Phi sources are handled
          * above, because they logically happen in the
          * predecessor.
          */
         bi_foreach_ssa_src(I, s) {
            if (BITSET_TEST(defined, I->src[s].value))
               continue;
            if (search_next_uses(&dists, I->src[s].value) < DIST_INFINITY)
               continue;

            assert(I->src[s].value < ctx->ssa_alloc);
            set_next_use(&dists, I->src[s].value, cycle);
         }

         /* Record defs */
         bi_foreach_ssa_dest(I, d) {
            assert(I->dest[d].value < ctx->ssa_alloc);
            BITSET_SET(defined, I->dest[d].value);
         }

         cycle += instr_cycles(I);
      }

      /* Apply transfer function to get our entry state. */
      foreach_next_use(&sb->next_use_out, node, dist) {
         set_next_use(&sb->next_use_in, node, dist_sum(dist, sb->cycles));
      }

      foreach_next_use(&dists, node, dist) {
         set_next_use(&sb->next_use_in, node, dist);
      }

      int i;
      BITSET_FOREACH_SET(i, defined, ctx->ssa_alloc) {
         set_next_use(&sb->next_use_in, i, DIST_INFINITY);
      }

      /* Propagate the live in of the successor (blk) to the live out of
       * predecessors.
       *
       * Phi nodes are logically on the control flow edge and act in parallel.
       * To handle when propagating, we kill writes from phis and make live the
       * corresponding sources.
       */
      bi_foreach_predecessor(blk, pred) {
         struct spill_block *sp = &blocks[(*pred)->index];
         copy_next_uses(&dists, &sb->next_use_in);

         /* Kill write */
         bi_foreach_phi_in_block(blk, I) {
            assert(I->dest[0].type == BI_INDEX_NORMAL);
            set_next_use(&dists, I->dest[0].value, DIST_INFINITY);
         }

         /* Make live the corresponding source */
         bi_foreach_phi_in_block(blk, I) {
            bi_index operand = I->src[bi_predecessor_index(blk, *pred)];
            if (operand.type == BI_INDEX_NORMAL)
               set_next_use(&dists, operand.value, 0);
         }

         /* Join by taking minimum */
         if (minimum_next_uses(&sp->next_use_out, &dists))
            bi_worklist_push_tail(&worklist, *pred);
      }
   }

   free(defined);
   u_worklist_fini(&worklist);
   destroy_next_uses(&dists);
}

static ATTRIBUTE_NOINLINE void
validate_next_use_info(UNUSED bi_context *ctx,
                       UNUSED struct spill_block *blocks)
{
#ifndef NDEBUG
   int i;

   bi_foreach_block(ctx, blk) {
      struct spill_block *sb = &blocks[blk->index];

      /* Invariant: next-use distance is finite iff the node is live */
      BITSET_FOREACH_SET(i, blk->ssa_live_in, ctx->ssa_alloc)
         assert(search_next_uses(&sb->next_use_in, i) < DIST_INFINITY);

      BITSET_FOREACH_SET(i, blk->ssa_live_out, ctx->ssa_alloc)
         assert(search_next_uses(&sb->next_use_out, i) < DIST_INFINITY);

      foreach_next_use(&sb->next_use_in, i, _)
         assert(BITSET_TEST(blk->ssa_live_in, i));

      foreach_next_use(&sb->next_use_out, i, _)
         assert(BITSET_TEST(blk->ssa_live_out, i));
   }
#endif
}

/*
 * Limit the register file W to maximum size m by evicting registers.
 */
static ATTRIBUTE_NOINLINE void
limit(struct spill_ctx *ctx, bi_instr *I, unsigned m)
{
   /* Nothing to do if we're already below the limit */
   if (ctx->nW <= m)
      return;

   /* Gather candidates for eviction. Note that next_uses gives IPs whereas
    * cmp_dist expects relative distances. This requires us to subtract ctx->ip
    * to ensure that cmp_dist works properly. Even though logically it shouldn't
    * affect the sorted order, practically this matters for correctness with
    * rematerialization. See the dist=0 test in cmp_dist.
    */
   struct candidate *candidates = alloca(ctx->nW * sizeof(struct candidate));
   unsigned j = 0;

   int i;
   BITSET_FOREACH_SET(i, ctx->W, ctx->n_alloc) {
      assert(j < ctx->nW);

      dist_t next_use = ctx->next_uses[i];
      if (next_use < DIST_INFINITY && next_use >= ctx->ip)
         next_use -= ctx->ip;
      else
         next_use = DIST_INFINITY;
      candidates[j++] = (struct candidate){
         .node = i,
         .dist = next_use,
      };
   }

   /* Sort by next-use distance */
   util_qsort_r(candidates, j, sizeof(struct candidate), cmp_dist, ctx);

   /* Evict what doesn't fit */
   unsigned new_weight = 0;

   for (i = 0; i < j; ++i) {
      unsigned v = candidates[i].node;
      unsigned comps = node_size(ctx, v);

      if ((new_weight + comps) <= m) {
         new_weight += comps;
      } else {
         /* Insert a spill if we haven't spilled before and there is
          * another use
          */
         if (!BITSET_TEST(ctx->S, v) && candidates[i].dist < DIST_INFINITY) {
            bi_cursor c = choose_spill_position(ctx, v, bi_before_instr(I));
            bi_builder b = bi_init_builder(ctx->shader, c);
            insert_spill(&b, ctx, v);
            BITSET_SET(ctx->S, v);
         }

         remove_W(ctx, v);

         /* We keep going in case we can pack in a scalar */
      }
   }
}


/*
 * validation code for next_ip info
 */
static void
validate_next_ip(struct spill_ctx *ctx, struct util_dynarray *local_next_ip)
{
#ifndef NDEBUG
   dist_t *next_ips = util_dynarray_element(local_next_ip, dist_t, 0);
   unsigned next_use_cursor =
      util_dynarray_num_elements(local_next_ip, dist_t);

   bi_foreach_instr_in_block(ctx->block, I) {
      if (I->op == BI_OPCODE_PHI)
         continue;
      bi_foreach_ssa_src_rev(I, s) {
         assert(next_use_cursor >= 1);

         unsigned next_ip = next_ips[--next_use_cursor];
         assert((next_ip == DIST_INFINITY) == I->src[s].kill_ssa);
      }
      bi_foreach_ssa_dest(I, d) {
         assert(next_use_cursor >= 1);
         unsigned next_ip = next_ips[--next_use_cursor];
         (void)next_ip;
      }
   }
   assert(next_use_cursor == 0 && "exactly sized");
#endif
}

/*
 * Insert spills/fills for a single basic block, following Belady's algorithm.
 * Corresponds to minAlgorithm from the paper.
 */
static ATTRIBUTE_NOINLINE void
min_algorithm(struct spill_ctx *ctx)
{
   struct spill_block *sblock = spill_block(ctx, ctx->block);
   struct util_dynarray local_next_ip;
   calculate_local_next_use(ctx, &local_next_ip);

   validate_next_ip(ctx, &local_next_ip);

   /* next_uses gives the distance from the start of the block, so prepopulate
    * with next_use_in.
    */
   foreach_next_use(&sblock->next_use_in, key, dist) {
      assert(key < ctx->n_alloc);
      ctx->next_uses[key] = dist;
   }

   dist_t *next_ips = util_dynarray_element(&local_next_ip, dist_t, 0);
   unsigned next_use_cursor =
      util_dynarray_num_elements(&local_next_ip, dist_t);

   /* Iterate each instruction in forward order */
   bi_foreach_instr_in_block(ctx->block, I) {
      assert(ctx->nW <= ctx->k && "invariant");

      /* Debug to check against our RA demand calculations */
      if (0) {
         printf("%u: ", ctx->nW);
         bi_print_instr(I, stdout);
      }

      /* Phis are special since they happen along the edge. When we initialized
       * W and S, we implicitly chose which phis are spilled. So, here we just
       * need to rewrite the phis to write into memory.
       *
       * Phi sources are handled later.
       */
      if (I->op == BI_OPCODE_PHI) {
         if (!BITSET_TEST(ctx->W, I->dest[0].value)) {
            I->dest[0] = bi_index_as_mem(I->dest[0], ctx);
         }

         ctx->ip += instr_cycles(I);
         continue;
      }

      /* Any source that is not in W needs to be reloaded. Gather the set R of
       * such values.
       */
      unsigned R[BI_MAX_SRCS];
      unsigned nR = 0;

      bi_foreach_ssa_src(I, s) {
         unsigned node = I->src[s].value;
         if (BITSET_TEST(ctx->W, node))
            continue;

         /* Mark this variable as needing a reload. */
         assert(node < ctx->n_alloc);
         assert(BITSET_TEST(ctx->S, node) && "must have been spilled");
         assert(nR < ARRAY_SIZE(R) && "maximum source count");
         R[nR++] = node;

         /* The inserted reload will add the value to the register file. */
         insert_W(ctx, node);
      }

      /* Limit W to make space for the sources we just added */
      limit(ctx, I, ctx->k);

      /* Update next-use distances for this instruction. Unlike the paper, we
       * prune dead values from W as we go. This doesn't affect correctness, but
       * it speeds up limit() on average.
       */
      bi_foreach_ssa_src_rev(I, s) {
         assert(next_use_cursor >= 1);

         unsigned next_ip = next_ips[--next_use_cursor];
         assert((next_ip == DIST_INFINITY) == I->src[s].kill_ssa);

         if (next_ip == DIST_INFINITY)
            remove_W_if_present(ctx, I->src[s].value);
         else
            ctx->next_uses[I->src[s].value] = next_ip;
      }

      bi_foreach_ssa_dest(I, d) {
         assert(next_use_cursor >= 1);
         unsigned next_ip = next_ips[--next_use_cursor];

         if (next_ip == DIST_INFINITY)
            remove_W_if_present(ctx, I->dest[d].value);
         else
            ctx->next_uses[I->dest[d].value] = next_ip;
      }

      /* Count how many registers we need for destinations. Because of
       * SSA form, destinations are unique.
       */
      unsigned dest_size = 0;
      bi_foreach_ssa_dest(I, d) {
         dest_size += node_size(ctx, I->dest[d].value);
      }

      /* Limit W to make space for the destinations. */
      limit(ctx, I, ctx->k - dest_size);

      /* Destinations are now in the register file */
      bi_foreach_ssa_dest(I, d) {
         insert_W(ctx, I->dest[d].value);
      }

      /* Add reloads for the sources in front of the instruction. We need to be
       * careful around exports, hoisting the reloads to before all exports.
       *
       * This is legal since all exports happen in parallel and all registers
       * are dead after the exports. The register file
       * must be big enough for everything exported, so it must be big enough
       * for all the reloaded values right before the parallel exports.
       */
      for (unsigned i = 0; i < nR; ++i) {
         insert_reload(ctx, ctx->block, bi_before_instr(I),
                       R[i]);
      }

      ctx->ip += instr_cycles(I);
   }

   assert(next_use_cursor == 0 && "exactly sized");

   int i;
   BITSET_FOREACH_SET(i, ctx->W, ctx->n_alloc)
      sblock->W_exit[sblock->nW_exit++] = i;

   unsigned nS = __bitset_count(ctx->S, BITSET_WORDS(ctx->n_alloc));
   sblock->S_exit = ralloc_array(ctx->memctx, unsigned, nS);

   BITSET_FOREACH_SET(i, ctx->S, ctx->n_alloc)
      sblock->S_exit[sblock->nS_exit++] = i;

   assert(nS == sblock->nS_exit);
   util_dynarray_fini(&local_next_ip);
}

static void
record_ssa_defs(bi_context *ctx, bi_instr **defs, bi_block **blocks)
{
   bi_foreach_block(ctx, b) {
      bi_foreach_instr_in_block(b, I) {
         if (I->nr_dests > 0 && bi_is_ssa(I->dest[0])) {
            for (uint32_t vi = 0; vi < I->nr_dests; ++vi) {
               const uint32_t v = I->dest[vi].value;

               if (defs[v] != NULL || blocks[v] != NULL) {
                  bi_print_instr(I, stderr);
                  fprintf(stderr, "before\n");
                  bi_print_instr(defs[v], stderr);
               }
               assert(defs[v] == NULL && "violating SSA");
               assert(blocks[v] == NULL && "violating SSA");
               defs[v] = I;
               blocks[v] = b;
            }
         }
      }
   }
}

/*
 * spill to keep the number of registers in use
 * below `k`
 * returns number of registers spilled
 */

unsigned
bi_spill_ssa(bi_context *ctx, unsigned k, unsigned spill_base)
{
   void *memctx = ralloc_context(NULL);
   unsigned spill_count = spill_base;
   unsigned max_temps = MIN_TEMPS_FOR_SPILL;

   /* calculate how many temporaries we may need */
   bi_foreach_instr_global(ctx, I) {
      /* we may need a temp to re-materialize */
      if (can_remat(I))
         max_temps++;
      /* we also may need temps to handle phis */
      if (I->op == BI_OPCODE_PHI)
         max_temps++;
   }

   dist_t *next_uses = rzalloc_array(memctx, dist_t, ctx->ssa_alloc + max_temps);
   bi_instr **remat = rzalloc_array(memctx, bi_instr *, ctx->ssa_alloc + max_temps);
   uint32_t *sizes = rzalloc_array(memctx, uint32_t, ctx->ssa_alloc + max_temps);

   /* now record instructions that can be easily re-materialized */
   /* while we're at it, calculate sizes too */
   bi_foreach_instr_global(ctx, I) {
      if (I->nr_dests == 0 || I->dest[0].type != BI_INDEX_NORMAL)
         continue;
      unsigned idx = I->dest[0].value;
      if (can_remat(I))
         remat[idx] = I;
      bi_foreach_ssa_dest(I, d) {
         idx = I->dest[d].value;
         assert(sizes[idx] == 0 && "SSA broken");
         switch (I->op) {
         case BI_OPCODE_PHI:
            break;
         default:
            sizes[idx] = bi_count_write_registers(I, d);
            break;
         }
      }
   }
   /* now that we know the rest of the sizes, find the sizes for PHI nodes */
   bi_foreach_block(ctx, block) {
      bi_foreach_phi_in_block(block, I) {
         if (I->dest[0].type != BI_INDEX_NORMAL)
            continue;
         unsigned idx = I->dest[0].value;
         sizes[idx] = 1;
         bi_foreach_ssa_src(I, s) {
            sizes[idx] = MAX2(sizes[idx], sizes[I->src[s].value]);
         }
      }
   }
   struct spill_block *blocks =
      rzalloc_array(memctx, struct spill_block, ctx->num_blocks);

   /* Step 1. Compute global next-use distances */
   global_next_use_distances(ctx, memctx, blocks);
   validate_next_use_info(ctx, blocks);

   /* we may need to allocate some temporaries for spilling PHIs, hence the max_temps */
   unsigned n = ctx->ssa_alloc + max_temps;
   BITSET_WORD *W = ralloc_array(memctx, BITSET_WORD, BITSET_WORDS(n));
   BITSET_WORD *S = ralloc_array(memctx, BITSET_WORD, BITSET_WORDS(n));
   uint32_t *spill_map = ralloc_array(memctx, uint32_t, n);
   uint32_t *mem_map = ralloc_array(memctx, uint32_t, n);
   BITSET_WORD *spill_map_store = BITSET_RZALLOC(memctx, n);

   bi_instr **ssa_defs = rzalloc_array(memctx, bi_instr *, n);
   bi_block **ssa_def_blocks = rzalloc_array(memctx, bi_block *, n);
   record_ssa_defs(ctx, ssa_defs, ssa_def_blocks);

   /* initialize to FFFFFFFF */
   memset(spill_map, 0xff, sizeof(uint32_t) * n);
   memset(mem_map, 0xff, sizeof(uint32_t) * n);

   /* Required for finding blocks belonging to loops. */
   bi_calc_dominance(ctx);

   bi_foreach_block(ctx, block) {
      memset(W, 0, BITSET_BYTES(n));
      memset(S, 0, BITSET_BYTES(n));

      struct spill_ctx sctx = {
         .memctx = memctx,
         .shader = ctx,
         .n_alloc = ctx->ssa_alloc,
         .remat = remat,
         .next_uses = next_uses,
         .block = block,
         .blocks = blocks,
         .k = k,
         .W = W,
         .S = S,
         .size = sizes,
         .spill_max = n,
         .spill_base = spill_base,
         .spill_map = spill_map,
         .spill_bytes = spill_count,
         .spill_map_store = spill_map_store,
         .mem_map = mem_map,
         .ssa_defs = ssa_defs,
         .ssa_def_blocks = ssa_def_blocks,
         .arch = ctx->arch,
      };

      compute_w_entry(&sctx);
      compute_s_entry(&sctx);
      min_algorithm(&sctx);
      spill_count = MAX2(spill_count, sctx.spill_bytes);
   }

   /* Now that all blocks are processed separately, stitch it together */
   bi_foreach_block(ctx, block) {
      struct spill_ctx sctx = {
         .memctx = memctx,
         .shader = ctx,
         .n_alloc = ctx->ssa_alloc,
         .remat = remat,
         .block = block,
         .blocks = blocks,
         .k = k,
         .W = W,
         .S = S,
         .size = sizes,
         .spill_max = n,
         .spill_base = spill_base,
         .spill_map = spill_map,
         .spill_bytes = spill_count,
         .spill_map_store = spill_map_store,
         .mem_map = mem_map,
         .ssa_defs = ssa_defs,
         .ssa_def_blocks = ssa_def_blocks,
         .arch = ctx->arch,
      };

      bi_foreach_predecessor(block, pred) {
         /* After spilling phi sources, insert coupling code */
         insert_coupling_code(&sctx, *pred, block);
      }
      spill_count = MAX2(spill_count, sctx.spill_bytes);
   }

   ralloc_free(memctx);
   return spill_count;
}
