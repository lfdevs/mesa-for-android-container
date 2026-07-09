/*
 * Copyright 2026 Intel Corporation
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2022 Collabora Ltd.
 * Copyright 2019 Broadcom
 * SPDX-License-Identifier: MIT
 */

/*
 * This file implements a simple pre-RA bottom-up list scheduler with the goal
 * of decreasing register pressure. On Xe2, this significantly reduces spilling.
 *
 * SSA form allows us to estimate register demand cheaply and accurately, which
 * theoretically [1] gives this algorithm the two Hippocratic properties:
 *
 * 1. Shaders with low register pressure are unaffected.
 * 2. Register pressure can only be decreased, never increased.
 *
 * In other words: first, do no harm.
 *
 * The heuristic itself is very simple: greedily choose instructions that
 * decrease liveness using a backwards list scheduler. This is far from optimal!
 * But thanks to the above properties, even a heuristic that picked random
 * instructions would be a win overall - by construction, we can only ever win.
 *
 * [1] In reality, neither property is strictly satisfied due to the messy
 * details of mapping our clean logical model onto Intel's many weird physical
 * register files. Nevertheless, the algorithm is well-motivated and the
 * empirical results on Xe2 are excellent.
 */

#include "util/bitset.h"
#include "util/ralloc.h"
#include "util/sparse_bitset.h"
#include "util/u_dynarray.h"
#include "jay_builder.h"
#include "jay_dag.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

struct sched_block {
   uint32_t first, last;
   int32_t max_pressure;
};

struct sched_ctx {
   /* Function we are currently scheduling */
   jay_function *func;

   struct jay_dag dag;
   struct jay_dag_iterator it;

   jay_inst **insts;
   struct u_sparse_bitset live;
   BITSET_WORD *seen;

   /* Array of node indices for the schedule we're building */
   struct util_dynarray schedule;

   struct sched_block *blocks;

   /* Current register demand */
   int32_t demand[JAY_NUM_SSA_FILES];
};

/* Cut down version of the function in jay_liveness.c */
static void
liveness_update(struct u_sparse_bitset *live, jay_inst *I)
{
   jay_foreach_dst_index(I, _, def) {
      u_sparse_bitset_clear(live, def);
   }

   jay_foreach_src_index(I, _, comp, index) {
      u_sparse_bitset_set(live, index);
   }
}

static void
populate_dag(struct sched_ctx *ctx, jay_block *block, uint32_t *def)
{
   uint32_t first_node_in_this_block = ctx->dag.node;

   /* TODO: Reorder memory instructions */
   uint32_t sidefx = 0, address = 0;

   jay_foreach_inst_in_block(block, I) {
      if (jay_op_starts_block(I->op)) {
         continue;
      } else if (jay_op_ends_block(I->op)) {
         break;
      }

      /* Uses depend on definitions. SSA form forbids WaR and WaW hazards */
      jay_foreach_src_index(I, s, c, index) {
         if (def[index] && def[index] >= first_node_in_this_block) {
            jay_dag_add_edge(&ctx->dag, def[index]);
         }
      }

      jay_foreach_dst_index(I, d, index) {
         def[index] = ctx->dag.node;
      }

      /* Serialize address register access until we have an address RA */
      bool use_a0 = I->dst.file == J_ADDRESS ||
                    I->op == JAY_OPCODE_SHUFFLE ||
                    I->op == JAY_OPCODE_VECTOR_EXTRACT;
      jay_foreach_src(I, s) {
         use_a0 |= I->src[s].file == J_ADDRESS;
      }

      if (use_a0) {
         jay_dag_add_edge(&ctx->dag, address);
         address = ctx->dag.node;
      }

      /* Serialize side effects for now, including SENDs which need to be
       * predicated away after a demote.
       */
      if ((I->op == JAY_OPCODE_SEND && !jay_send_pure(I)) ||
          I->op == JAY_OPCODE_SCHEDULE_BARRIER ||
          I->op == JAY_OPCODE_INIT_HELPERS ||
          I->op == JAY_OPCODE_DEMOTE ||
          I->op == JAY_OPCODE_HELPER_SEL ||
          (I->op == JAY_OPCODE_SEND &&
           ctx->func->shader->helpers_tracked &&
           jay_send_skip_helpers(I))) {

         jay_dag_add_edge(&ctx->dag, sidefx);
         sidefx = ctx->dag.node;
      }

      ctx->insts[ctx->dag.node] = I;
      jay_dag_next_node(&ctx->dag);
   }

   ctx->blocks[block->index].first = first_node_in_this_block;
   ctx->blocks[block->index].last = ctx->dag.node - 1;
}

/*
 * Due to multiple register files, register demand is a vector. Our dynamic
 * register file partitioning sometimes justifies modelling demand as a single
 * scalar, where each file has a weight determined here.
 */
static signed
weighted_demand(struct sched_ctx *ctx, signed *demands)
{
   signed weighted = 0;

   jay_foreach_ssa_file(file) {
      signed scale = file == J_ADDRESS  ? 0 :
                     file & JAY_UNIFORM ? 1 :
                                          ctx->func->shader->dispatch_width;
      weighted += demands[file] * scale;
   }

   return weighted;
}

/*
 * Calculate the change in register pressure from scheduling a given
 * instuction. Based on jay_calculate_register_demands, but without the use of
 * kill-bits since we are reordering instructions.
 */
static void
adjust_demand_before(struct sched_ctx *ctx, jay_inst *I, signed *demand)
{
   /* Make destinations live */
   jay_foreach_dst(I, dst) {
      demand[dst.file] -= jay_num_values(dst);
   }
}

static void
adjust_demand_after(struct sched_ctx *ctx, jay_inst *I, signed *demand)
{
   unsigned counter = 0;

   /* Dead destinations are those written by the instruction but killed
    * immediately after the instruction finishes.
    */
   jay_foreach_dst_index(I, dst, index) {
      assert(dst.file < JAY_NUM_SSA_FILES);
      demand[dst.file] += !u_sparse_bitset_test(&ctx->live, index);
   }

   /* Late-kill sources. We precomputed the deduplication info and stashed it in
    * the I->last_use bitfield for convenience.
    */
   jay_foreach_src_index(I, s, c, index) {
      if (BITSET_TEST(I->last_use, counter)) {
         assert(I->src[s].file < JAY_NUM_SSA_FILES);
         demand[I->src[s].file] += !u_sparse_bitset_test(&ctx->live, index);
      }

      counter++;
   }

}

/*
 * Choose the next instuction, bottom-up. For now we use a simple greedy
 * heuristic: choose the instuction that has the best effect on liveness.
 */
static uint32_t
choose_inst(struct sched_ctx *s)
{
   int32_t min_delta = INT32_MAX;
   uint32_t best = 0;

   util_dynarray_foreach(&s->it.heads, uint32_t, head) {
      jay_inst *I = s->insts[*head];

      /* To minimize pressure, consider the effect on liveness. */
      int32_t deltas[JAY_NUM_SSA_FILES] = { 0 };
      adjust_demand_after(s, I, deltas);
      adjust_demand_before(s, I, deltas);
      int32_t delta = weighted_demand(s, deltas);

      /* As a tiebreaker (only), sink flag writes to reduce specifically flag
       * pressure, because spilling flags costs extra instructions and GPR
       * pressure. This is a mildly positive heuristic.
       */
      delta *= 2;
      if (jay_is_null(I->cond_flag)) {
         delta++;
      }

      if (delta <= min_delta) {
         best = *head;
         min_delta = delta;
      }
   }

   return best;
}

static void
gather_block_info(struct sched_ctx *s, jay_block *block, void *memctx)
{
   int32_t demand[JAY_NUM_SSA_FILES] = { 0 };
   signed max_pressure = 0;

   u_sparse_bitset_free(&s->live);
   u_sparse_bitset_dup_with_ctx(&s->live, &block->live_out, memctx);

   jay_foreach_inst_in_block_rev(block, I) {
      if (jay_op_starts_block(I->op)) {
         break;
      } else if (jay_op_ends_block(I->op)) {
         continue;
      }

      unsigned counter = 0;

      /* Filter duplicates as we go */
      BITSET_ZERO(I->last_use);

      jay_foreach_src_index(I, _, c, index) {
         if (!BITSET_TEST(s->seen, index)) {
            BITSET_SET(I->last_use, counter);
         }

         BITSET_SET(s->seen, index);
         counter++;
      }

      jay_foreach_src_index(I, _, c, index) {
         BITSET_CLEAR(s->seen, index);
      }

      adjust_demand_after(s, I, demand);
      max_pressure = MAX2(weighted_demand(s, demand), max_pressure);
      adjust_demand_before(s, I, demand);
      liveness_update(&s->live, I);
   }

   s->blocks[block->index].max_pressure = max_pressure;
}

static void
schedule_block(jay_block *block, struct sched_ctx *s, void *memctx)
{
   s->it.dag = &s->dag;
   jay_dag_iterate(&s->it, s->blocks[block->index].first,
                   s->blocks[block->index].last);

   util_dynarray_clear(&s->schedule);

   memset(s->demand, 0, JAY_NUM_SSA_FILES * sizeof(s->demand[0]));
   u_sparse_bitset_free(&s->live);
   u_sparse_bitset_dup_with_ctx(&s->live, &block->live_out, memctx);

   while (s->it.heads.size) {
      int32_t node = choose_inst(s);

      adjust_demand_after(s, s->insts[node], s->demand);

      /* Toss schedules that blow up register pressure as we go */
      if (weighted_demand(s, s->demand) >=
          s->blocks[block->index].max_pressure) {
         jay_dag_iterator_reset(&s->it);
         return;
      }

      adjust_demand_before(s, s->insts[node], s->demand);
      liveness_update(&s->live, s->insts[node]);

      jay_dag_take_head(&s->it, node);
      util_dynarray_append(&s->schedule, node);
   }

   /* Apply schedule */
   util_dynarray_foreach(&s->schedule, uint32_t, node) {
      jay_remove_instruction(s->insts[*node]);
   }

   jay_builder b = jay_init_builder(s->func, jay_before_block(block));

   util_dynarray_foreach_reverse(&s->schedule, uint32_t, node) {
      jay_builder_insert(&b, s->insts[*node]);
   }
}

static void
pass(jay_function *f)
{
   jay_compute_liveness(f);
   jay_calculate_register_demands(f);

   void *memctx = ralloc_context(NULL);
   void *linctx = linear_context(memctx);

   uint32_t nr_inst = 1;
   jay_foreach_inst_in_func(f, _, I) {
      ++nr_inst;
   }

   BITSET_WORD *seen = BITSET_LINEAR_ZALLOC(linctx, f->ssa_alloc);
   struct sched_ctx sctx = { .seen = seen, .func = f };
   uint32_t *def = linear_zalloc_array(linctx, uint32_t, f->ssa_alloc);
   sctx.insts = linear_alloc_array(linctx, jay_inst *, nr_inst);
   sctx.blocks = linear_zalloc_array(linctx, struct sched_block, f->num_blocks);
   jay_dag_init(&sctx.dag, memctx, nr_inst);
   jay_dag_iterator_init(&sctx.it, &sctx.dag);

   unsigned ugpr_per_grf = jay_ugpr_per_grf(f->shader);
   unsigned ugpr_per_gpr = jay_grf_per_gpr(f->shader) * ugpr_per_grf;

   /* Build the DAG for the whole program */
   jay_foreach_block(f, block) {
      populate_dag(&sctx, block, def);
   }

   jay_foreach_block(f, block) {
      /* Gather reference statistics about the program performance */
      gather_block_info(&sctx, block, memctx);

      /* Do pressure-only scheduling only on blocks that might spill, to
       * minimize harm. We conservatively use 104 GRFs as the threshold instead
       * of 128 to leave wiggle room for flag RA and late lowerings.
       *
       * We treat flags as GPR demand conservatively since they spill to GPRs.
       */
      unsigned demand_ugpr = block->demand_max[UGPR];
      unsigned demand_gpr = block->demand_max[GPR] +
                            block->demand_max[FLAG] +
                            block->demand_max[UFLAG];

      if (((demand_gpr * ugpr_per_gpr) + demand_ugpr) >= (104 * ugpr_per_grf)) {
         f->prioritize_pressure = true;
         schedule_block(block, &sctx, memctx);
      }
   }

   util_dynarray_fini(&sctx.schedule);
   ralloc_free(memctx);
}

JAY_DEFINE_FUNCTION_PASS(jay_schedule_pressure, pass)
