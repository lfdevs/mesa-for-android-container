/*
 * Copyright 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler.h"

struct lower_spill_ctx {
   bi_context *shader;
   uint32_t *sizes;
   /* If bi_index::memory, tls_loc[idx.value] is valid. */
   uint32_t *tls_loc;
   /* Bytes of spill memory used so far. */
   uint32_t spill_count;
};

static void
lower_memmov(struct lower_spill_ctx* ctx, bi_instr *I, uint32_t tls_base)
{
   assert(I->op == BI_OPCODE_MEMMOV);
   assert(I->nr_srcs == 1 && I->nr_dests == 1);
   assert(I->src[0].memory || I->dest[0].memory);

   bi_builder b = bi_init_builder(ctx->shader, bi_before_instr(I));

   bi_index src = I->src[0];
   bi_index dst = I->dest[0];

   if (I->src[0].memory) {
      unsigned offset = ctx->tls_loc[src.value];
      unsigned bits = ctx->sizes[src.value] * 32;
      assert(bits <= 128);
      bi_load_tl(&b, bits, dst, tls_base + offset);
   } else {
      unsigned offset = ctx->tls_loc[dst.value];
      unsigned bits = ctx->sizes[dst.value] * 32;
      assert(bits <= 128);
      bi_store_tl(&b, bits, src, tls_base + offset);
   }

   bi_remove_instruction(I);
}

static void
lower_mem_phi(struct lower_spill_ctx* ctx, bi_instr *I, uint32_t tls_base)
{
   assert(I->op == BI_OPCODE_PHI);
   assert(I->nr_dests == 1);

   /* PHIs get lowered in bi_out_of_ssa, which expects memory operands to
    * provide the actual TLS offset as bi_index::value.
    */

   unsigned words = 1;

   if (I->dest[0].memory)
      words = MAX2(words, ctx->sizes[I->dest[0].value]);

   bi_foreach_src(I, s) {
      if (I->src[s].memory)
         words = MAX2(words, ctx->sizes[I->src[s].value]);
   }

   assert(words <= 4);
   I->table = words;

   if (I->dest[0].memory) {
      const bi_index dst = I->dest[0];
      /* Preserve the memory value long enough to look up its assigned TLS
       * slot. bi_out_of_ssa uses I->table to lower all words of the PHI.
       */
      I->dest[0].value = tls_base + ctx->tls_loc[dst.value];
   }

   bi_foreach_src(I, s) {
      const bi_index src = I->src[s];
      if (!src.memory)
         continue;

      assert(ctx->tls_loc[src.value] != UINT32_MAX && "Undefined source");
      I->src[s].value = tls_base + ctx->tls_loc[src.value];
   }
}

static void
assign_tls_locations(struct lower_spill_ctx *ctx) {
   bi_foreach_instr_global(ctx->shader, I) {
      bi_foreach_ssa_dest(I, d) {
         bi_index dst = I->dest[d];
         if (!dst.memory)
            continue;

         assert(I->op == BI_OPCODE_MEMMOV || I->op == BI_OPCODE_PHI);
         assert(ctx->tls_loc[dst.value] == UINT32_MAX && "Broken SSA");

         unsigned size = ctx->sizes[dst.value];
         unsigned alignment = (size <= 1) ? 4 : (size == 2) ? 8 : 16;

         ctx->spill_count = ALIGN_POT(ctx->spill_count, alignment);
         ctx->tls_loc[dst.value] = ctx->spill_count;
         ctx->spill_count += 4 * size;
      }
   }
}

unsigned
bi_lower_spill(bi_context* ctx, uint32_t tls_base) {
   void* memctx = ralloc_context(NULL);

   uint32_t *sizes = rzalloc_array(memctx, uint32_t, ctx->ssa_alloc);
   uint32_t *tls_loc = ralloc_array(memctx, uint32_t, ctx->ssa_alloc);
   memset(tls_loc, 0xff, sizeof(uint32_t) * ctx->ssa_alloc);

   bi_record_sizes(ctx, sizes);

   struct lower_spill_ctx lctx = {
      .shader = ctx,
      .sizes = sizes,
      .tls_loc = tls_loc,
      .spill_count = 0,
   };

   assign_tls_locations(&lctx);

   bi_foreach_instr_global_safe(ctx, I) {
      switch (I->op) {
      case BI_OPCODE_MEMMOV:
         lower_memmov(&lctx, I, tls_base);
         break;
      case BI_OPCODE_PHI:
         lower_mem_phi(&lctx, I, tls_base);
         break;
      default:
         break;
      }
   }

   ralloc_free(memctx);

   return lctx.spill_count;
}
