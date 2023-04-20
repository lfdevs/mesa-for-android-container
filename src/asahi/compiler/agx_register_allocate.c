/*
 * Copyright 2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_builder.h"
#include "agx_compiler.h"

/* SSA-based register allocator */

struct ra_ctx {
   agx_context *shader;
   agx_block *block;
   uint8_t *ssa_to_reg;
   uint8_t *ncomps;
   BITSET_WORD *visited;
   BITSET_WORD *used_regs;

   /* For affinities */
   agx_instr **src_to_collect;

   /* Maximum number of registers that RA is allowed to use */
   unsigned bound;
};

/** Returns number of registers written by an instruction */
unsigned
agx_write_registers(const agx_instr *I, unsigned d)
{
   unsigned size = agx_size_align_16(I->dest[d].size);

   switch (I->op) {
   case AGX_OPCODE_ITER:
      assert(1 <= I->channels && I->channels <= 4);
      return I->channels * size;

   case AGX_OPCODE_TEXTURE_LOAD:
   case AGX_OPCODE_TEXTURE_SAMPLE:
      /* Even when masked out, these clobber 4 registers */
      return 4 * size;

   case AGX_OPCODE_DEVICE_LOAD:
   case AGX_OPCODE_LOCAL_LOAD:
   case AGX_OPCODE_LD_TILE:
      return util_bitcount(I->mask) * size;

   case AGX_OPCODE_LDCF:
      return 6;
   case AGX_OPCODE_COLLECT:
      return I->nr_srcs * agx_size_align_16(I->src[0].size);
   default:
      return size;
   }
}

static inline enum agx_size
agx_split_width(const agx_instr *I)
{
   enum agx_size width = ~0;

   agx_foreach_dest(I, d) {
      if (I->dest[d].type == AGX_INDEX_NULL)
         continue;
      else if (width != ~0)
         assert(width == I->dest[d].size);
      else
         width = I->dest[d].size;
   }

   assert(width != ~0 && "should have been DCE'd");
   return width;
}

unsigned
agx_read_registers(const agx_instr *I, unsigned s)
{
   unsigned size = agx_size_align_16(I->src[s].size);

   switch (I->op) {
   case AGX_OPCODE_SPLIT:
      return I->nr_dests * agx_size_align_16(agx_split_width(I));

   case AGX_OPCODE_DEVICE_STORE:
   case AGX_OPCODE_LOCAL_STORE:
   case AGX_OPCODE_ST_TILE:
      if (s == 0)
         return util_bitcount(I->mask) * size;
      else
         return size;

   case AGX_OPCODE_ZS_EMIT:
      if (s == 1) {
         /* Depth (bit 0) is fp32, stencil (bit 1) is u16 in the hw but we pad
          * up to u32 for simplicity
          */
         return 2 * (!!(I->zs & 1) + !!(I->zs & 2));
      } else {
         return 1;
      }

   case AGX_OPCODE_TEXTURE_LOAD:
   case AGX_OPCODE_TEXTURE_SAMPLE:
      if (s == 0) {
         /* Coordinates. We handle layer + sample index as 32-bit even when only
          * the lower 16-bits are present.
          */
         switch (I->dim) {
         case AGX_DIM_1D:
            return 2 * 1;
         case AGX_DIM_1D_ARRAY:
            return 2 * 2;
         case AGX_DIM_2D:
            return 2 * 2;
         case AGX_DIM_2D_ARRAY:
            return 2 * 3;
         case AGX_DIM_2D_MS:
            return 2 * 3;
         case AGX_DIM_3D:
            return 2 * 3;
         case AGX_DIM_CUBE:
            return 2 * 3;
         case AGX_DIM_CUBE_ARRAY:
            return 2 * 4;
         case AGX_DIM_2D_MS_ARRAY:
            return 2 * 3;
         }

         unreachable("Invalid texture dimension");
      } else if (s == 1) {
         /* LOD */
         if (I->lod_mode == AGX_LOD_MODE_LOD_GRAD) {
            switch (I->dim) {
            case AGX_DIM_1D:
            case AGX_DIM_1D_ARRAY:
               return 2 * 2 * 1;
            case AGX_DIM_2D:
            case AGX_DIM_2D_ARRAY:
            case AGX_DIM_2D_MS_ARRAY:
            case AGX_DIM_2D_MS:
               return 2 * 2 * 2;
            case AGX_DIM_CUBE:
            case AGX_DIM_CUBE_ARRAY:
            case AGX_DIM_3D:
               return 2 * 2 * 3;
            }

            unreachable("Invalid texture dimension");
         } else {
            return 1;
         }
      } else if (s == 4) {
         /* Compare/offset */
         return 2 * ((!!I->shadow) + (!!I->offset));
      } else {
         return size;
      }

   case AGX_OPCODE_ATOMIC:
   case AGX_OPCODE_LOCAL_ATOMIC:
      if (s == 0 && I->atomic_opc == AGX_ATOMIC_OPC_CMPXCHG)
         return size * 2;
      else
         return size;

   default:
      return size;
   }
}

static unsigned
find_regs(BITSET_WORD *used_regs, unsigned count, unsigned align, unsigned max)
{
   assert(count >= 1);

   for (unsigned reg = 0; reg + count <= max; reg += align) {
      if (!BITSET_TEST_RANGE(used_regs, reg, reg + count - 1))
         return reg;
   }

   /* Couldn't find a free register, dump the state of the register file */
   fprintf(stderr, "Failed to find register of size %u aligned %u max %u.\n",
           count, align, max);

   fprintf(stderr, "Register file:\n");
   for (unsigned i = 0; i < BITSET_WORDS(max); ++i)
      fprintf(stderr, "    %08X\n", used_regs[i]);

   unreachable("Could not find a free register");
}

/*
 * Loop over live-in values at the start of the block and mark their registers
 * as in-use. We process blocks in dominance order, so this handles everything
 * but loop headers.
 *
 * For loop headers, this handles the forward edges but not the back edge.
 * However, that's okay: we don't want to reserve the registers that are
 * defined within the loop, because then we'd get a contradiction. Instead we
 * leave them available and then they become fixed points of a sort.
 */
static void
reserve_live_in(struct ra_ctx *rctx)
{
   int i;
   BITSET_FOREACH_SET(i, rctx->block->live_in, rctx->shader->alloc) {
      /* Skip values defined in loops when processing the loop header */
      if (!BITSET_TEST(rctx->visited, i))
         continue;

      for (unsigned j = 0; j < rctx->ncomps[i]; ++j)
         BITSET_SET(rctx->used_regs, rctx->ssa_to_reg[i] + j);
   }
}

static void
assign_regs(struct ra_ctx *rctx, agx_index v, unsigned reg)
{
   assert(reg < rctx->bound && "must not overflow register file");
   assert(v.type == AGX_INDEX_NORMAL && "only SSA gets registers allocated");
   rctx->ssa_to_reg[v.value] = reg;

   assert(!BITSET_TEST(rctx->visited, v.value) && "SSA violated");
   BITSET_SET(rctx->visited, v.value);

   assert(rctx->ncomps[v.value] >= 1);
   unsigned end = reg + rctx->ncomps[v.value] - 1;
   assert(!BITSET_TEST_RANGE(rctx->used_regs, reg, end) && "no interference");
   BITSET_SET_RANGE(rctx->used_regs, reg, end);
}

static unsigned
affinity_base_of_collect(struct ra_ctx *rctx, agx_instr *collect, unsigned src)
{
   unsigned src_reg = rctx->ssa_to_reg[collect->src[src].value];
   unsigned src_offset = src * agx_size_align_16(collect->src[src].size);

   if (src_reg >= src_offset)
      return src_reg - src_offset;
   else
      return ~0;
}

static unsigned
pick_regs(struct ra_ctx *rctx, agx_instr *I, unsigned d)
{
   agx_index idx = I->dest[d];
   assert(idx.type == AGX_INDEX_NORMAL);

   unsigned count = agx_write_registers(I, d);
   unsigned align = agx_size_align_16(idx.size);
   assert(count >= 1);

   /* Try to allocate collects compatibly with their sources */
   if (I->op == AGX_OPCODE_COLLECT) {
      agx_foreach_ssa_src(I, s) {
         assert(BITSET_TEST(rctx->visited, I->src[s].value) &&
                "registers assigned in an order compatible with dominance "
                "and this is not a phi node, so we have assigned a register");

         unsigned base = affinity_base_of_collect(rctx, I, s);
         if (base >= rctx->bound || (base + count) > rctx->bound)
            continue;

         /* Unaligned destinations can happen when dest size > src size */
         if (base % align)
            continue;

         if (!BITSET_TEST_RANGE(rctx->used_regs, base, base + count - 1))
            return base;
      }
   }

   /* Try to allocate sources of collects contiguously */
   if (rctx->src_to_collect[idx.value] != NULL) {
      agx_instr *collect = rctx->src_to_collect[idx.value];

      assert(count == align && "collect sources are scalar");

      /* Find our offset in the collect. If our source is repeated in the
       * collect, this may not be unique. We arbitrarily choose the first.
       */
      unsigned our_source = ~0;
      agx_foreach_ssa_src(collect, s) {
         if (agx_is_equiv(collect->src[s], idx)) {
            our_source = s;
            break;
         }
      }

      assert(our_source < collect->nr_srcs && "source must be in the collect");

      /* See if we can allocate compatibly with any source of the collect */
      agx_foreach_ssa_src(collect, s) {
         if (!BITSET_TEST(rctx->visited, collect->src[s].value))
            continue;

         /* Determine where the collect should start relative to the source */
         unsigned base = affinity_base_of_collect(rctx, collect, s);
         if (base >= rctx->bound)
            continue;

         unsigned our_reg = base + (our_source * align);

         /* Don't allocate past the end of the register file */
         if ((our_reg + align) > rctx->bound)
            continue;

         /* If those registers are free, then choose them */
         if (!BITSET_TEST_RANGE(rctx->used_regs, our_reg, our_reg + align - 1))
            return our_reg;
      }

      unsigned collect_align = agx_size_align_16(collect->dest[0].size);
      unsigned offset = our_source * align;

      /* Prefer ranges of the register file that leave room for all sources of
       * the collect contiguously.
       */
      for (unsigned base = 0; base + (collect->nr_srcs * align) <= rctx->bound;
           base += collect_align) {
         if (!BITSET_TEST_RANGE(rctx->used_regs, base,
                                base + (collect->nr_srcs * align) - 1))
            return base + offset;
      }

      /* Try to respect the alignment requirement of the collect destination,
       * which may be greater than the sources (e.g. pack_64_2x32_split). Look
       * for a register for the source such that the collect base is aligned.
       */
      if (collect_align > align) {
         for (unsigned reg = offset; reg + collect_align <= rctx->bound;
              reg += collect_align) {
            if (!BITSET_TEST_RANGE(rctx->used_regs, reg, reg + count - 1))
               return reg;
         }
      }
   }

   /* Default to any contiguous sequence of registers */
   return find_regs(rctx->used_regs, count, align, rctx->bound);
}

/** Assign registers to SSA values in a block. */

static void
agx_ra_assign_local(struct ra_ctx *rctx)
{
   BITSET_DECLARE(used_regs, AGX_NUM_REGS) = {0};

   agx_block *block = rctx->block;
   uint8_t *ssa_to_reg = rctx->ssa_to_reg;
   uint8_t *ncomps = rctx->ncomps;
   rctx->used_regs = used_regs;

   reserve_live_in(rctx);

   /* Force the nesting counter r0l live throughout shaders using control flow.
    * This could be optimized (sync with agx_calc_register_demand).
    */
   if (rctx->shader->any_cf)
      BITSET_SET(used_regs, 0);

   agx_foreach_instr_in_block(block, I) {
      /* Optimization: if a split contains the last use of a vector, the split
       * can be removed by assigning the destinations overlapping the source.
       */
      if (I->op == AGX_OPCODE_SPLIT && I->src[0].kill) {
         unsigned reg = ssa_to_reg[I->src[0].value];
         unsigned width = agx_size_align_16(agx_split_width(I));

         agx_foreach_dest(I, d) {
            /* Free up the source */
            unsigned offset_reg = reg + (d * width);
            BITSET_CLEAR_RANGE(used_regs, offset_reg, offset_reg + width - 1);

            /* Assign the destination where the source was */
            if (!agx_is_null(I->dest[d]))
               assign_regs(rctx, I->dest[d], offset_reg);
         }

         continue;
      } else if (I->op == AGX_OPCODE_PRELOAD) {
         /* We must coalesce all preload moves */
         assert(I->dest[0].size == I->src[0].size);
         assert(I->src[0].type == AGX_INDEX_REGISTER);

         assign_regs(rctx, I->dest[0], I->src[0].value);
         continue;
      }

      /* First, free killed sources */
      agx_foreach_ssa_src(I, s) {
         if (I->src[s].kill) {
            unsigned reg = ssa_to_reg[I->src[s].value];
            unsigned count = ncomps[I->src[s].value];

            assert(count >= 1);
            BITSET_CLEAR_RANGE(used_regs, reg, reg + count - 1);
         }
      }

      /* Next, assign destinations one at a time. This is always legal
       * because of the SSA form.
       */
      agx_foreach_ssa_dest(I, d) {
         assign_regs(rctx, I->dest[d], pick_regs(rctx, I, d));
      }
   }

   STATIC_ASSERT(sizeof(block->regs_out) == sizeof(used_regs));
   memcpy(block->regs_out, used_regs, sizeof(used_regs));
}

/*
 * Lower phis to parallel copies at the logical end of a given block. If a block
 * needs parallel copies inserted, a successor of the block has a phi node. To
 * have a (nontrivial) phi node, a block must have multiple predecessors. So the
 * edge from the block to the successor (with phi) is not the only edge entering
 * the successor. Because the control flow graph has no critical edges, this
 * edge must therefore be the only edge leaving the block, so the block must
 * have only a single successor.
 */
static void
agx_insert_parallel_copies(agx_context *ctx, agx_block *block)
{
   bool any_succ = false;
   unsigned nr_phi = 0;

   /* Phi nodes logically happen on the control flow edge, so parallel copies
    * are added at the end of the predecessor */
   agx_builder b = agx_init_builder(ctx, agx_after_block_logical(block));

   agx_foreach_successor(block, succ) {
      assert(nr_phi == 0 && "control flow graph has a critical edge");

      agx_foreach_phi_in_block(succ, phi) {
         assert(!any_succ && "control flow graph has a critical edge");
         nr_phi++;
      }

      any_succ = true;

      /* Nothing to do if there are no phi nodes */
      if (nr_phi == 0)
         continue;

      unsigned pred_index = agx_predecessor_index(succ, block);

      /* Create a parallel copy lowering all the phi nodes */
      struct agx_copy *copies = calloc(sizeof(*copies), nr_phi);

      unsigned i = 0;

      agx_foreach_phi_in_block(succ, phi) {
         agx_index dest = phi->dest[0];
         agx_index src = phi->src[pred_index];

         assert(dest.type == AGX_INDEX_REGISTER);
         assert(dest.size == src.size);

         copies[i++] = (struct agx_copy){
            .dest = dest.value,
            .src = src,
         };
      }

      agx_emit_parallel_copies(&b, copies, nr_phi);

      free(copies);
   }
}

void
agx_ra(agx_context *ctx)
{
   unsigned *alloc = calloc(ctx->alloc, sizeof(unsigned));

   agx_compute_liveness(ctx);
   uint8_t *ssa_to_reg = calloc(ctx->alloc, sizeof(uint8_t));
   uint8_t *ncomps = calloc(ctx->alloc, sizeof(uint8_t));
   agx_instr **src_to_collect = calloc(ctx->alloc, sizeof(agx_instr *));
   BITSET_WORD *visited = calloc(BITSET_WORDS(ctx->alloc), sizeof(BITSET_WORD));

   agx_foreach_instr_global(ctx, I) {
      /* Record collects so we can coalesce when assigning */
      if (I->op == AGX_OPCODE_COLLECT) {
         agx_foreach_ssa_src(I, s) {
            src_to_collect[I->src[s].value] = I;
         }
      }

      agx_foreach_ssa_dest(I, d) {
         unsigned v = I->dest[d].value;
         assert(ncomps[v] == 0 && "broken SSA");
         ncomps[v] = agx_write_registers(I, d);
      }
   }

   /* Assign registers in dominance-order. This coincides with source-order due
    * to a NIR invariant, so we do not need special handling for this.
    */
   agx_foreach_block(ctx, block) {
      agx_ra_assign_local(&(struct ra_ctx){
         .shader = ctx,
         .block = block,
         .ssa_to_reg = ssa_to_reg,
         .src_to_collect = src_to_collect,
         .ncomps = ncomps,
         .visited = visited,
         .bound = AGX_NUM_REGS,
      });
   }

   for (unsigned i = 0; i < ctx->alloc; ++i) {
      if (ncomps[i])
         ctx->max_reg = MAX2(ctx->max_reg, ssa_to_reg[i] + ncomps[i] - 1);
   }

   /* Vertex shaders preload the vertex/instance IDs (r5, r6) even if the shader
    * don't use them. Account for that so the preload doesn't clobber GPRs.
    */
   if (ctx->nir->info.stage == MESA_SHADER_VERTEX)
      ctx->max_reg = MAX2(ctx->max_reg, 6 * 2);

   agx_foreach_instr_global(ctx, ins) {
      agx_foreach_ssa_src(ins, s) {
         unsigned v = ssa_to_reg[ins->src[s].value];
         agx_replace_src(ins, s, agx_register(v, ins->src[s].size));
      }

      agx_foreach_ssa_dest(ins, d) {
         unsigned v = ssa_to_reg[ins->dest[d].value];
         ins->dest[d] =
            agx_replace_index(ins->dest[d], agx_register(v, ins->dest[d].size));
      }
   }

   agx_foreach_instr_global_safe(ctx, ins) {
      /* Lower away RA pseudo-instructions */
      agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));

      if (ins->op == AGX_OPCODE_COLLECT) {
         assert(ins->dest[0].type == AGX_INDEX_REGISTER);
         unsigned base = ins->dest[0].value;
         unsigned width = agx_size_align_16(ins->src[0].size);

         struct agx_copy *copies = alloca(sizeof(copies[0]) * ins->nr_srcs);
         unsigned n = 0;

         /* Move the sources */
         agx_foreach_src(ins, i) {
            if (agx_is_null(ins->src[i]) || ins->src[i].type == AGX_INDEX_UNDEF)
               continue;
            assert(ins->src[i].size == ins->src[0].size);

            copies[n++] = (struct agx_copy){
               .dest = base + (i * width),
               .src = ins->src[i],
            };
         }

         agx_emit_parallel_copies(&b, copies, n);
         agx_remove_instruction(ins);
         continue;
      } else if (ins->op == AGX_OPCODE_SPLIT) {
         assert(ins->src[0].type == AGX_INDEX_REGISTER);
         unsigned base = ins->src[0].value;
         unsigned width = agx_size_align_16(agx_split_width(ins));

         struct agx_copy copies[4];
         assert(ins->nr_dests <= ARRAY_SIZE(copies));

         unsigned n = 0;

         /* Move the sources */
         agx_foreach_dest(ins, i) {
            if (ins->dest[i].type != AGX_INDEX_REGISTER)
               continue;

            copies[n++] = (struct agx_copy){
               .dest = ins->dest[i].value,
               .src = agx_register(base + (i * width), ins->dest[i].size),
            };
         }

         /* Lower away */
         agx_builder b = agx_init_builder(ctx, agx_after_instr(ins));
         agx_emit_parallel_copies(&b, copies, n);
         agx_remove_instruction(ins);
         continue;
      }
   }

   /* Insert parallel copies lowering phi nodes */
   agx_foreach_block(ctx, block) {
      agx_insert_parallel_copies(ctx, block);
   }

   agx_foreach_instr_global_safe(ctx, I) {
      switch (I->op) {
      /* Pseudoinstructions for RA must be removed now */
      case AGX_OPCODE_PHI:
      case AGX_OPCODE_PRELOAD:
         agx_remove_instruction(I);
         break;

      /* Coalesced moves can be removed */
      case AGX_OPCODE_MOV:
         if (I->src[0].type == AGX_INDEX_REGISTER &&
             I->dest[0].size == I->src[0].size &&
             I->src[0].value == I->dest[0].value) {

            assert(I->dest[0].type == AGX_INDEX_REGISTER);
            agx_remove_instruction(I);
         }
         break;

      /* Writes to the nesting counter lowered to the real register */
      case AGX_OPCODE_NEST: {
         agx_builder b = agx_init_builder(ctx, agx_before_instr(I));
         agx_mov_to(&b, agx_register(0, AGX_SIZE_16), I->src[0]);
         agx_remove_instruction(I);
         break;
      }

      default:
         break;
      }
   }

   free(src_to_collect);
   free(ssa_to_reg);
   free(ncomps);
   free(visited);
   free(alloc);
}
