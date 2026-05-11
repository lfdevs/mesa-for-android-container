/*
 * Copyright 2026 Intel Corporation
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/*
 * Implementation of "Simple and Efficient
 * Construction of Static Single Assignment Form", also by Braun et al.
 * https://link.springer.com/content/pdf/10.1007/978-3-642-37051-9_6.pdf
 */

#include "util/bitset.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

struct incomplete_phi {
   jay_def old;
   unsigned new;
};

struct phi {
   jay_block *block;
   unsigned *src;
   jay_def old;
   unsigned dst;
};

struct ctx {
   /* Array of index->index maps with the remapped definition at block end */
   struct hash_table_u64 **defs;
   struct hash_table_u64 *remap;
   struct util_dynarray phis, indices, *incomplete_phis;
   BITSET_WORD *sealed;
   void *linctx;
   unsigned alloc, idx_i;
};

#define jay_repair_foreach_phi(ctx, phi)                                       \
   util_dynarray_foreach(&(ctx)->phis, struct phi, phi)                        \
      if (phi->block != NULL)

static unsigned lookup(struct ctx *ctx, jay_block *block, jay_def def);

static unsigned
remap_idx(struct ctx *ctx, unsigned idx)
{
   /* TODO: Switch to union-find */
   void *remapped;
   while ((remapped = _mesa_hash_table_u64_search(ctx->remap, idx))) {
      idx = (uintptr_t) remapped;
   }

   return idx;
}

static bool
try_remove_trivial_phi(struct ctx *ctx, struct phi *phi)
{
   unsigned same = 0;
   for (unsigned i = 0; i < jay_num_predecessors(phi->block, phi->old.file);
        ++i) {
      unsigned src = remap_idx(ctx, phi->src[i]);
      if (same && src != same && src != phi->dst) {
         /* Nontrivial */
         return false;
      }

      if (src != phi->dst) {
         same = src;
      }
   }

   _mesa_hash_table_u64_insert(ctx->remap, phi->dst, (void *) (uintptr_t) same);
   phi->block = NULL;
   return true;
}

static void
add_phi(struct ctx *ctx, jay_block *block, jay_def src, unsigned dst)
{
   unsigned i = 0, n = jay_num_predecessors(block, src.file);
   unsigned *srcs = linear_alloc_array(ctx->linctx, unsigned, n);
   jay_foreach_predecessor(block, pred, src.file) {
      assert(i < n);
      srcs[i++] = lookup(ctx, *pred, src);
   }

   struct phi tmpl = { .block = block, .old = src, .dst = dst, .src = srcs };
   if (!try_remove_trivial_phi(ctx, &tmpl)) {
      util_dynarray_append(&ctx->phis, tmpl);
   }
}

static unsigned
lookup(struct ctx *ctx, jay_block *block, jay_def def)
{
   /* Lookup within a block */
   struct hash_table_u64 *ht = ctx->defs[block->index];
   void *local = _mesa_hash_table_u64_search(ht, jay_index(def));
   if (local) {
      return (uintptr_t) local;
   }

   /* For a single predecessor, we can recurse without adding a phi. */
   bool insert_phi = jay_num_predecessors(block, def.file) > 1;
   unsigned val = insert_phi ?
                     ctx->alloc++ :
                     lookup(ctx, jay_first_predecessor(block, def.file), def);

   _mesa_hash_table_u64_insert(ctx->defs[block->index], jay_index(def),
                               (void *) (uintptr_t) val);

   if (block->loop_header && !BITSET_TEST(ctx->sealed, block->index)) {
      struct incomplete_phi tmpl = { .old = def, .new = val };
      util_dynarray_append(&ctx->incomplete_phis[block->index], tmpl);
   } else if (insert_phi) {
      add_phi(ctx, block, def, val);
   }

   return val;
}

static void
remap(struct ctx *ctx, jay_builder *b, jay_def *inout)
{
   jay_def def = *inout;
   unsigned reg = def.reg;
   jay_foreach_index(def, c, index) {
      unsigned el = ctx->idx_i++;
      assert(el < util_dynarray_num_elements(&ctx->indices, unsigned));
      unsigned idx = *util_dynarray_element(&ctx->indices, unsigned, el);
      idx = remap_idx(ctx, idx);
      jay_insert_channel(b, inout, c, jay_scalar(def.file, idx));
   }

   /* We run after flag RA, so preserve flag registers */
   if (jay_is_flag(def)) {
      inout->reg = reg;
   }
}

void
jay_repair_ssa(jay_function *func)
{
   jay_builder b = jay_init_builder(func, jay_before_function(func));
   void *memctx = ralloc_context(NULL);
   void *linctx = linear_context(memctx);
   BITSET_WORD *sealed = BITSET_LINEAR_ZALLOC(linctx, func->num_blocks);
   struct ctx ctx = { .sealed = sealed, .alloc = 1, .linctx = linctx };
   unsigned *phi_remap = linear_zalloc_array(linctx, unsigned, func->ssa_alloc);

   ctx.remap = _mesa_hash_table_u64_create(memctx);
   ctx.defs =
      linear_alloc_array(linctx, struct hash_table_u64 *, func->num_blocks);
   ctx.incomplete_phis =
      linear_alloc_array(linctx, struct util_dynarray, func->num_blocks);

   jay_foreach_block(func, block) {
      ctx.defs[block->index] = _mesa_hash_table_u64_create(memctx);
      util_dynarray_init(&ctx.incomplete_phis[block->index], memctx);
   }

   util_dynarray_init(&ctx.phis, memctx);
   util_dynarray_init(&ctx.indices, memctx);

   jay_foreach_block(func, block) {
      jay_foreach_inst_in_block(block, I) {
         jay_foreach_src_index(I, s, c, index) {
            unsigned val = lookup(&ctx, block, jay_extract(I->src[s], c));
            util_dynarray_append(&ctx.indices, val);
         }

         jay_foreach_dst_index(I, d, index) {
            unsigned val = ctx.alloc++;
            util_dynarray_append(&ctx.indices, val);
            if (I->op == JAY_OPCODE_PHI_DST) {
               phi_remap[index] = val;
            }

            _mesa_hash_table_u64_insert(ctx.defs[block->index], index,
                                        (void *) (uintptr_t) val);
         }
      }

      /* Seal loop headers after processing the back edge */
      jay_foreach_successor(block, succ, GPR) {
         if (succ->loop_header && succ->index <= block->index) {
            util_dynarray_foreach(&ctx.incomplete_phis[succ->index],
                                  struct incomplete_phi, el) {
               add_phi(&ctx, succ, el->old, el->new);
            }

            assert(!BITSET_TEST(sealed, succ->index) && "unique backedge");
            BITSET_SET(sealed, succ->index);
         }
      }
   }

   /* Optimize trivial phis resulting from backedges. Use-lists would avoid the
    * fixed point algorithm but this should be good enough for now.
    */
   bool progress;
   do {
      progress = false;
      jay_repair_foreach_phi(&ctx, phi) {
         progress |= try_remove_trivial_phi(&ctx, phi);
      }
   } while (progress);

   /* Now apply everything */
   jay_foreach_block(func, block) {
      jay_foreach_phi_src_in_block(block, I) {
         jay_set_phi_src_index(I, phi_remap[jay_phi_src_index(I)]);
      }

      jay_foreach_inst_in_block(block, I) {
         jay_foreach_ssa_src(I, s) {
            remap(&ctx, &b, &I->src[s]);
         }

         remap(&ctx, &b, &I->dst);
         remap(&ctx, &b, &I->cond_flag);
      }
   }

   jay_repair_foreach_phi(&ctx, phi) {
      b.cursor = jay_before_block(phi->block);
      jay_PHI_DST(&b, jay_scalar(phi->old.file, phi->dst));

      unsigned i = 0;
      jay_foreach_predecessor(phi->block, pred, phi->old.file) {
         b.cursor = jay_before_jump(*pred);
         unsigned idx = remap_idx(&ctx, phi->src[i++]);
         jay_PHI_SRC_u32(&b, jay_scalar(phi->old.file, idx), phi->dst);
      }
   }

   func->ssa_alloc = ctx.alloc;
   ralloc_free(memctx);
}
