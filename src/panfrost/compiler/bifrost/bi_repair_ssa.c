/*
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

#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "bi_builder.h"
#include "compiler.h"
#include "bi_opcodes.h"

struct repair_block {
   /* For a loop header, whether phis operands have been added */
   bool sealed;

   /* Sparse map: variable name -> bi_index.
    *
    * Definition of a variable at the end of the block.
    */
   struct hash_table_u64 *defs;
};

struct repair_ctx {
   bi_context *shader;

   /* Number of variables */
   unsigned n;

   /* Information on blocks indexed in source order */
   struct repair_block *blocks;
};

static inline struct repair_block *
repair_block(struct repair_ctx *ctx, bi_block *block)
{
   return &ctx->blocks[block->index];
}

static void
record_write(struct repair_ctx *ctx, bi_block *block, unsigned node,
             bi_index val)
{
   assert(node < ctx->n);

   /* The value always gets used as a source, where we don't want the
    * destination "swizzle" and other modifiers that are applied on write.
    */
   val.swizzle = BI_SWIZZLE_H01;
   val.neg = false;
   val.abs = false;

   struct hash_table_u64 *defs = repair_block(ctx, block)->defs;
   _mesa_hash_table_u64_insert(defs, node,
                               ralloc_memdup(defs, &val, sizeof(val)));
}

static void add_phi_operands(struct repair_ctx *ctx, bi_block *block,
                             bi_instr *phi, bi_index node);

static bi_index
resolve_read(struct repair_ctx *ctx, bi_block *block, bi_index node)
{
   struct repair_block *rb = repair_block(ctx, block);

   /* Local value numbering */
   assert(node.type == BI_INDEX_NORMAL);
   bi_index *local = _mesa_hash_table_u64_search(rb->defs, node.value);

   if (local) {
      assert(!bi_is_null(*local));
      return *local;
   }

   /* Global value numbering. readValueRecursive in the paper */
   unsigned nr_preds = bi_num_predecessors(block);
   bi_index val;

   assert(nr_preds > 0);

   /* Loop headers are not in the "sealedBlock" set in the paper. To
    * handle, we insert an incomplete phi to be filled in after the rest of
    * the loop is processed.
    */
   if (block->loop_header && !rb->sealed) {
      val = bi_temp_like(ctx->shader, node);
      bi_builder b = bi_init_builder(ctx->shader, bi_before_block(block));
      bi_instr *phi = bi_phi_to(&b, val, nr_preds);
      phi->shadow = true;

      /* Stash the variable in for an intrusive incompletePhis map */
      phi->index = node.value + 1;
   } else if (nr_preds == 1) {
      /* No phi needed */
      bi_block *pred =
         *util_dynarray_element(&block->predecessors, bi_block *, 0);
      val = resolve_read(ctx, pred, node);
   } else {
      /* Insert phi first to break cycles */
      val = bi_temp_like(ctx->shader, node);
      bi_builder b = bi_init_builder(ctx->shader, bi_before_block(block));
      bi_instr *phi = bi_phi_to(&b, val, nr_preds);
      phi->shadow = true;
      record_write(ctx, block, node.value, val);
      add_phi_operands(ctx, block, phi, node);
   }

   assert(!bi_is_null(val));
   assert(val.swizzle == BI_SWIZZLE_H01);
   record_write(ctx, block, node.value, val);
   return val;
}

static void
add_phi_operands(struct repair_ctx *ctx, bi_block *block, bi_instr *phi,
                 bi_index node)
{
   /* Add phi operands */
   bi_foreach_predecessor(block, pred) {
      unsigned s = bi_predecessor_index(block, *pred);
      phi->src[s] = resolve_read(ctx, *pred, node);
   }
}

static void
seal_block(struct repair_ctx *ctx, bi_block *block)
{
   bi_foreach_phi_in_block(block, phi) {
      /* We use phi->index as a sideband to pass the variable name. */
      if (phi->index) {
         bi_index var = bi_get_index(phi->index - 1);
         var.memory = phi->dest[0].memory;
         add_phi_operands(ctx, block, phi, var);
         phi->index = 0;
      }
   }

   repair_block(ctx, block)->sealed = true;
}

static void
seal_loop_headers(struct repair_ctx *ctx, struct bi_block *exit)
{
   bi_foreach_successor(exit, succ) {
      /* Only loop headers need to be sealed late */
      if (!succ->loop_header)
         continue;

      /* Check if all predecessors have been processed */
      bool any_unprocessed = false;

      bi_foreach_predecessor(succ, P) {
         if ((*P)->index > exit->index) {
            any_unprocessed = true;
            break;
         }
      }

      /* Seal once all predecessors are processed */
      if (!any_unprocessed)
         seal_block(ctx, succ);
   }
}

static void
bi_opt_trivial_phi(bi_context *ctx)
{
   bi_index *remap = calloc(ctx->ssa_alloc, sizeof(*remap));
   for (;;) {
      bool progress = false;
      memset(remap, 0, ctx->ssa_alloc * sizeof(*remap));

      bi_foreach_block(ctx, block) {
         bi_foreach_phi_in_block_safe(block, phi) {
            bi_index same = bi_null();
            bool all_same = true;

            bi_foreach_src(phi, s) {
               const bi_index src = phi->src[s];
               /* TODO: Handle cycles faster */
               if (bi_is_ssa(src) && !bi_is_null(remap[src.value])) {
                  all_same = false;
                  break;
               }

               /* Same value or self-reference */
               if (bi_is_equiv(src, same) ||
                   bi_is_equiv(src, phi->dest[0]))
                  continue;

               if (!bi_is_null(same)) {
                  all_same = false;
                  break;
               }

               same = src;
            }

            /* Only optimize trivial phis with normal sources. It is possible
             * to optimize something like `phi #0, #0` but...
             *
             * 1. It would inadvently propagate constants which may be invalid.
             *    Copyprop knows the rules for this, but we don't here.
             *
             * 2. These trivial phis should be optimized at the NIR level. This
             *    pass is just to clean up spilling.
             *
             * So skip them for correctness in case NIR misses something (which
             * can happen depending on pass order).
             */
            if (all_same && same.type == BI_INDEX_NORMAL) {
               remap[phi->dest[0].value] = same;
               bi_remove_instruction(phi);
               progress = true;
            }
         }
      }

      if (!progress)
         break;

      bi_foreach_instr_global(ctx, I) {
         bi_foreach_ssa_src(I, s) {
            if (!bi_is_null(remap[I->src[s].value])) {
               bi_replace_src(I, s, remap[I->src[s].value]);
            }
         }
      }
   }

   free(remap);
}

void
bi_repair_ssa(bi_context *ctx)
{
   struct repair_block *blocks =
      rzalloc_array(NULL, struct repair_block, ctx->num_blocks);

   bi_foreach_block(ctx, block) {
      struct repair_block *rb = &blocks[block->index];
      rb->defs = _mesa_hash_table_u64_create(blocks);
   }

   unsigned n = ctx->ssa_alloc;

   bi_foreach_block(ctx, block) {
      struct repair_ctx rctx = {
         .shader = ctx,
         .n = n,
         .blocks = blocks,
      };

      bi_foreach_instr_in_block(block, I) {
         /* Repair SSA for the instruction */
         if (I->op != BI_OPCODE_PHI) {
            bi_foreach_ssa_src(I, s) {
               assert(I->src[s].value < n);
               bi_replace_src(I, s, resolve_read(&rctx, block, I->src[s]));
            }
         }

         bi_foreach_ssa_dest(I, d) {
            unsigned handle = I->dest[d].value;

            /* Skip phis that we just created when processing loops */
            if (handle >= n) {
               assert(I->op == BI_OPCODE_PHI);
               continue;
            }

            I->dest[d] =
               bi_replace_index(I->dest[d], bi_temp_like(ctx, I->dest[d]));

            record_write(&rctx, block, handle, I->dest[d]);
         }
      }

      seal_loop_headers(&rctx, block);
   }

   bi_foreach_block(ctx, block) {
      bi_foreach_phi_in_block(block, phi) {
         /* The kill bit is invalid (and meaningless) for phis. Liveness
          * analysis does not produce it. However, we're ingesting broken SSA
          * where we can have random kill bits set on phis. Strip them as part
          * of the SSA repair.
          *
          * The register allocator depends on this for correctness.
          */
         phi->dest[0].kill_ssa = false;

         bi_foreach_src(phi, s) {
            phi->src[s].kill_ssa = false;
         }

         /* Skip the phis that we just created */
         if (phi->shadow) {
            phi->shadow = false;
            continue;
         }

         bi_foreach_ssa_src(phi, s) {
            /* Phis (uniquely) read their sources in their corresponding
             * predecessors, so chain through for that.
             */
            bi_block *read_block =
               *util_dynarray_element(&block->predecessors, bi_block *, s);

            assert(phi->src[s].value < n);

            struct repair_ctx rctx = {
               .shader = ctx,
               .n = n,
               .blocks = blocks,
            };

            bi_replace_src(phi, s,
                           resolve_read(&rctx, read_block, phi->src[s]));
         }
      }
   }

   ralloc_free(blocks);

   bi_opt_trivial_phi(ctx);
   bi_reindex_ssa(ctx);
}
