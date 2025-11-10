/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

/* NOTE: see https://gitlab.freedesktop.org/freedreno/freedreno/-/wikis/A5xx-Queries */

#include "freedreno_query_acc.h"
#include "freedreno_resource.h"

#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_query.h"

#include "fd6_pack.h"

template <chip CHIP>
static void
emit_counter_barrier(fd_cs &cs)
{
   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   if (CHIP >= A8XX) {
      fd_pkt7(cs, CP_BARRIER, 1).add(1);
   }
}

/* g++ is a picky about offsets that cannot be resolved at compile time, so
 * roll our own __offsetof()
 */
#define __offsetof(type, field)                                                \
   ({ type _x = {}; ((uint8_t *)&_x.field) - ((uint8_t *)&_x);})

struct PACKED fd6_query_sample {
   struct fd_acc_query_sample base;

   /* The RB_SAMPLE_COUNTER_BASE destination needs to be 16-byte aligned: */
   uint64_t pad;

   uint64_t start;
   uint64_t result;
   uint64_t stop;
};
FD_DEFINE_CAST(fd_acc_query_sample, fd6_query_sample);

/* offset of a single field of an array of fd6_query_sample: */
#define query_sample_idx(aq, idx, field)                                       \
   fd_resource((aq)->prsc)->bo,                                                \
      (idx * sizeof(struct fd6_query_sample)) +                                \
         offsetof(struct fd6_query_sample, field)

/* offset of a single field of fd6_query_sample: */
#define query_sample(aq, field) query_sample_idx(aq, 0, field)

/*
 * Occlusion Query:
 *
 * OCCLUSION_COUNTER and OCCLUSION_PREDICATE differ only in how they
 * interpret results
 */

template <chip CHIP>
static void
occlusion_resume(struct fd_acc_query *aq, struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;
   fd_cs cs(batch->draw);

   ASSERT_ALIGNED(struct fd6_query_sample, start, 16);

   fd_pkt4(cs, 1)
      .add(A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));

   if (!ctx->screen->info->props.has_event_write_sample_count) {
      fd_pkt4(cs, 2)
         .add(A6XX_RB_SAMPLE_COUNTER_BASE(query_sample(aq, start)));

      fd6_event_write<CHIP>(ctx, cs, FD_ZPASS_DONE);

      /* Copied from blob's cmdstream, not sure why it is done. */
      if (CHIP == A7XX) {
         fd6_event_write<CHIP>(ctx, cs, FD_CCU_CLEAN_DEPTH);
      }
   } else {
      fd_pkt7(cs, CP_EVENT_WRITE7, 3)
         .add(CP_EVENT_WRITE7_0(
            .event = ZPASS_DONE,
            .write_sample_count = true,
         ))
         .add(EV_DST_RAM_CP_EVENT_WRITE7_1(query_sample(aq, start)));

      fd_pkt7(cs, CP_EVENT_WRITE7, 3)
         .add(CP_EVENT_WRITE7_0(
            .event = ZPASS_DONE,
            .write_sample_count = true,
            .sample_count_end_offset = true,
            .write_accum_sample_count_diff = true,
         ))
         .add(EV_DST_RAM_CP_EVENT_WRITE7_1(query_sample(aq, start)));
   }

   ctx->occlusion_queries_active++;

   /* Just directly bash the gen specific LRZ dirty bit, since we don't
    * need to re-emit any other LRZ related state:
    */
   ctx->gen_dirty |= FD6_GROUP_LRZ;
}

template <chip CHIP>
static void
occlusion_pause(struct fd_acc_query *aq, struct fd_batch *batch) assert_dt
{
   struct fd_context *ctx = batch->ctx;
   fd_cs cs(batch->draw);

   if (!ctx->screen->info->props.has_event_write_sample_count) {
      fd_pkt7(cs, CP_MEM_WRITE, 4)
         .add(A5XX_CP_MEM_WRITE_ADDR(query_sample(aq, stop)))
         .add(0xffffffff)
         .add(0xffffffff);

      fd_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
   }

   fd_pkt4(cs, 1)
      .add(A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));

   ASSERT_ALIGNED(struct fd6_query_sample, stop, 16);

   if (!ctx->screen->info->props.has_event_write_sample_count) {
      fd_pkt4(cs, 2)
         .add(A6XX_RB_SAMPLE_COUNTER_BASE(query_sample(aq, stop)));

      fd6_event_write<CHIP>(batch->ctx, cs, FD_ZPASS_DONE);

      /* To avoid stalling in the draw buffer, emit code the code to compute the
       * counter delta in the epilogue ring.
       */
      fd_cs epilogue(fd_batch_get_tile_epilogue(batch));

      fd_pkt7(epilogue, CP_WAIT_REG_MEM, 6)
         .add(CP_WAIT_REG_MEM_0(.function = WRITE_NE, .poll = POLL_MEMORY))
         .add(CP_WAIT_REG_MEM_POLL_ADDR(query_sample(aq, stop)))
         .add(CP_WAIT_REG_MEM_3(.ref = 0xffffffff))
         .add(CP_WAIT_REG_MEM_4(.mask = 0xffffffff))
         .add(CP_WAIT_REG_MEM_5(.delay_loop_cycles = 16));

      /* result += stop - start: */
      fd_pkt7(epilogue, CP_MEM_TO_MEM, 9)
         .add(CP_MEM_TO_MEM_0(.neg_c = true, ._double = true))
         .add(CP_MEM_TO_MEM_DST(query_sample(aq, result)))
         .add(CP_MEM_TO_MEM_SRC_A(query_sample(aq, result)))
         .add(CP_MEM_TO_MEM_SRC_B(query_sample(aq, stop)))
         .add(CP_MEM_TO_MEM_SRC_C(query_sample(aq, start)));
   } else {
      fd_pkt7(cs, CP_EVENT_WRITE7, 3)
         .add(CP_EVENT_WRITE7_0(
            .event = ZPASS_DONE,
            .write_sample_count = true,
         ))
         .add(EV_DST_RAM_CP_EVENT_WRITE7_1(query_sample(aq, stop)));

      fd_pkt7(cs, CP_EVENT_WRITE7, 3)
         .add(CP_EVENT_WRITE7_0(
            .event = ZPASS_DONE,
            .write_sample_count = true,
            .sample_count_end_offset = true,
            .write_accum_sample_count_diff = true,
         ))
         /* Note: SQE is adding offsets to the iova, SAMPLE_COUNT_END_OFFSET causes
          * the result to be written to iova+16, and WRITE_ACCUM_SAMP_COUNT_DIFF
          * does *(iova + 8) += *(iova + 16) - *iova
          *
          * It just so happens this is the layout we already to for start/result/stop
          * So we just give the start address in all cases.
          */
         .add(EV_DST_RAM_CP_EVENT_WRITE7_1(query_sample(aq, start)));
   }

   assert(ctx->occlusion_queries_active > 0);
   ctx->occlusion_queries_active--;

   /* Just directly bash the gen specific LRZ dirty bit, since we don't
    * need to re-emit any other LRZ related state:
    */
   ctx->gen_dirty |= FD6_GROUP_LRZ;
}

static void
occlusion_counter_result(struct fd_acc_query *aq,
                         struct fd_acc_query_sample *s,
                         union pipe_query_result *result)
{
   struct fd6_query_sample *sp = fd6_query_sample(s);
   result->u64 = sp->result;
}

static void
occlusion_counter_result_resource(struct fd_acc_query *aq, struct fd_ringbuffer *ring,
                                  enum pipe_query_value_type result_type,
                                  int index, struct fd_resource *dst,
                                  unsigned offset)
{
   copy_result(ring, result_type, dst, offset, fd_resource(aq->prsc),
               offsetof(struct fd6_query_sample, result));
}

static void
occlusion_predicate_result(struct fd_acc_query *aq,
                           struct fd_acc_query_sample *s,
                           union pipe_query_result *result)
{
   struct fd6_query_sample *sp = fd6_query_sample(s);
   result->b = !!sp->result;
}

static void
occlusion_predicate_result_resource(struct fd_acc_query *aq, struct fd_ringbuffer *ring,
                                    enum pipe_query_value_type result_type,
                                    int index, struct fd_resource *dst,
                                    unsigned offset)
{
   fd_cs cs(ring);

   /* This is a bit annoying but we need to turn the result into a one or
    * zero.. to do this use a CP_COND_WRITE to overwrite the result with
    * a one if it is non-zero.  This doesn't change the results if the
    * query is also read on the CPU (ie. occlusion_predicate_result()).
    */
   fd_pkt7(cs, CP_COND_WRITE5, 9)
      .add(CP_COND_WRITE5_0(
         .function = WRITE_NE,
         .poll = POLL_MEMORY,
         .write_memory = true
      ))
      .add(CP_COND_WRITE5_POLL_ADDR(query_sample(aq, result)))
      .add(CP_COND_WRITE5_3(.ref = 0))
      .add(CP_COND_WRITE5_4(.mask = ~0))
      .add(CP_COND_WRITE5_WRITE_ADDR(query_sample(aq, result)))
      .add(1)
      .add(0);

   copy_result(cs, result_type, dst, offset, fd_resource(aq->prsc),
               offsetof(struct fd6_query_sample, result));
}

template <chip CHIP>
static const struct fd_acc_sample_provider occlusion_counter = {
   .query_type = PIPE_QUERY_OCCLUSION_COUNTER,
   .size = sizeof(struct fd6_query_sample),
   .resume = occlusion_resume<CHIP>,
   .pause = occlusion_pause<CHIP>,
   .result = occlusion_counter_result,
   .result_resource = occlusion_counter_result_resource,
};

template <chip CHIP>
static const struct fd_acc_sample_provider occlusion_predicate = {
   .query_type = PIPE_QUERY_OCCLUSION_PREDICATE,
   .size = sizeof(struct fd6_query_sample),
   .resume = occlusion_resume<CHIP>,
   .pause = occlusion_pause<CHIP>,
   .result = occlusion_predicate_result,
   .result_resource = occlusion_predicate_result_resource,
};

template <chip CHIP>
static const struct fd_acc_sample_provider occlusion_predicate_conservative = {
   .query_type = PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE,
   .size = sizeof(struct fd6_query_sample),
   .resume = occlusion_resume<CHIP>,
   .pause = occlusion_pause<CHIP>,
   .result = occlusion_predicate_result,
   .result_resource = occlusion_predicate_result_resource,
};

/*
 * Timestamp Queries:
 */

template <chip CHIP>
static void
timestamp_resume(struct fd_acc_query *aq, struct fd_batch *batch)
{
   fd_cs cs(batch->draw);

   fd6_record_ts<CHIP>(cs, query_sample(aq, start));
}

template <chip CHIP>
static void
time_elapsed_pause(struct fd_acc_query *aq, struct fd_batch *batch) assert_dt
{
   fd_cs cs(batch->draw);

   fd6_record_ts<CHIP>(cs, query_sample(aq, stop));

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   /* result += stop - start: */
   fd_pkt7(cs, CP_MEM_TO_MEM, 9)
      .add(CP_MEM_TO_MEM_0(.neg_c = true, ._double = true))
      .add(CP_MEM_TO_MEM_DST(query_sample(aq, result)))
      .add(CP_MEM_TO_MEM_SRC_A(query_sample(aq, result)))
      .add(CP_MEM_TO_MEM_SRC_B(query_sample(aq, stop)))
      .add(CP_MEM_TO_MEM_SRC_C(query_sample(aq, start)));
}

static void
timestamp_pause(struct fd_acc_query *aq, struct fd_batch *batch)
{
   /* We captured a timestamp in timestamp_resume(), nothing to do here. */
}

/* timestamp logging for u_trace: */
template <chip CHIP>
static void
record_timestamp(struct fd_ringbuffer *ring, struct fd_bo *bo, unsigned offset)
{
   fd_cs cs(ring);

   cs.attach_bo(bo);
   fd6_record_ts<CHIP>(cs, bo, offset);
}

static void
time_elapsed_accumulate_result(struct fd_acc_query *aq,
                               struct fd_acc_query_sample *s,
                               union pipe_query_result *result)
{
   struct fd6_query_sample *sp = fd6_query_sample(s);
   result->u64 = ticks_to_ns(sp->result);
}

static void
time_elapsed_result_resource(struct fd_acc_query *aq, struct fd_ringbuffer *ring,
                             enum pipe_query_value_type result_type,
                             int index, struct fd_resource *dst,
                             unsigned offset)
{
   // TODO ticks_to_ns conversion would require spinning up a compute shader?
   copy_result(ring, result_type, dst, offset, fd_resource(aq->prsc),
               offsetof(struct fd6_query_sample, result));
}

static void
timestamp_accumulate_result(struct fd_acc_query *aq,
                            struct fd_acc_query_sample *s,
                            union pipe_query_result *result)
{
   struct fd6_query_sample *sp = fd6_query_sample(s);
   result->u64 = ticks_to_ns(sp->start);
}

static void
timestamp_result_resource(struct fd_acc_query *aq, struct fd_ringbuffer *ring,
                          enum pipe_query_value_type result_type,
                          int index, struct fd_resource *dst,
                          unsigned offset)
{
   // TODO ticks_to_ns conversion would require spinning up a compute shader?
   copy_result(ring, result_type, dst, offset, fd_resource(aq->prsc),
               offsetof(struct fd6_query_sample, start));
}

template <chip CHIP>
static const struct fd_acc_sample_provider time_elapsed = {
   .query_type = PIPE_QUERY_TIME_ELAPSED,
   .always = true,
   .size = sizeof(struct fd6_query_sample),
   .resume = timestamp_resume<CHIP>,
   .pause = time_elapsed_pause<CHIP>,
   .result = time_elapsed_accumulate_result,
   .result_resource = time_elapsed_result_resource,
};

/* NOTE: timestamp query isn't going to give terribly sensible results
 * on a tiler.  But it is needed by qapitrace profile heatmap.  If you
 * add in a binning pass, the results get even more non-sensical.  So
 * we just return the timestamp on the last tile and hope that is
 * kind of good enough.
 */

template <chip CHIP>
static const struct fd_acc_sample_provider timestamp = {
   .query_type = PIPE_QUERY_TIMESTAMP,
   .always = true,
   .size = sizeof(struct fd6_query_sample),
   .resume = timestamp_resume<CHIP>,
   .pause = timestamp_pause,
   .result = timestamp_accumulate_result,
   .result_resource = timestamp_result_resource,
};

struct PACKED fd6_pipeline_stats_sample {
   struct fd_acc_query_sample base;

   uint64_t start, stop, result;
};
FD_DEFINE_CAST(fd_acc_query_sample, fd6_pipeline_stats_sample);

#define stats_sample(aq, field) \
   fd_resource((aq)->prsc)->bo, offsetof(struct fd6_pipeline_stats_sample, field)

/* Mapping of counters to pipeline stats:
 *
 *   Gallium (PIPE_STAT_QUERY_x) | Vulkan (VK_QUERY_PIPELINE_STATISTIC_x_BIT) | hw counter
 *   ----------------------------+--------------------------------------------+----------------
 *   IA_VERTICES                 | INPUT_ASSEMBLY_VERTICES                    | RBBM_PRIMCTR_0
 *   IA_PRIMITIVES               | INPUT_ASSEMBLY_PRIMITIVES                  | RBBM_PRIMCTR_1
 *   VS_INVOCATIONS              | VERTEX_SHADER_INVOCATIONS                  | RBBM_PRIMCTR_2
 *   GS_INVOCATIONS              | GEOMETRY_SHADER_INVOCATIONS                | RBBM_PRIMCTR_5
 *   GS_PRIMITIVES               | GEOMETRY_SHADER_PRIMITIVES                 | RBBM_PRIMCTR_6
 *   C_INVOCATIONS               | CLIPPING_INVOCATIONS                       | RBBM_PRIMCTR_7
 *   C_PRIMITIVES                | CLIPPING_PRIMITIVES                        | RBBM_PRIMCTR_8
 *   PS_INVOCATIONS              | FRAGMENT_SHADER_INVOCATIONS                | RBBM_PRIMCTR_9
 *   HS_INVOCATIONS              | TESSELLATION_CONTROL_SHADER_PATCHES        | RBBM_PRIMCTR_3
 *   DS_INVOCATIONS              | TESSELLATION_EVALUATION_SHADER_INVOCATIONS | RBBM_PRIMCTR_4
 *   CS_INVOCATIONS              | COMPUTE_SHADER_INVOCATIONS                 | RBBM_PRIMCTR_10
 */

enum stats_type {
   STATS_PRIMITIVE,
   STATS_FRAGMENT,
   STATS_COMPUTE,
};

static const struct {
   enum fd_gpu_event start, stop;
} stats_counter_events[] = {
      [STATS_PRIMITIVE] = { FD_START_PRIMITIVE_CTRS, FD_STOP_PRIMITIVE_CTRS },
      [STATS_FRAGMENT]  = { FD_START_FRAGMENT_CTRS,  FD_STOP_FRAGMENT_CTRS },
      [STATS_COMPUTE]   = { FD_START_COMPUTE_CTRS,   FD_STOP_COMPUTE_CTRS },
};

static enum stats_type
get_stats_type(struct fd_acc_query *aq)
{
   if (aq->provider->query_type == PIPE_QUERY_PRIMITIVES_GENERATED)
      return STATS_PRIMITIVE;

   switch (aq->base.index) {
   case PIPE_STAT_QUERY_PS_INVOCATIONS: return STATS_FRAGMENT;
   case PIPE_STAT_QUERY_CS_INVOCATIONS: return STATS_COMPUTE;
   default:
      return STATS_PRIMITIVE;
   }
}

template <chip CHIP>
static unsigned
stats_counter_reg(struct fd_acc_query *aq)
{
#define COUNTER_REG(name) __RBBM_PIPESTAT_ ## name <CHIP>({}).reg

   if (aq->provider->query_type == PIPE_QUERY_PRIMITIVES_GENERATED)
      return COUNTER_REG(CINVOCATIONS);

   switch (aq->base.index) {
   case PIPE_STAT_QUERY_IA_VERTICES:    return COUNTER_REG(IAVERTICES);
   case PIPE_STAT_QUERY_IA_PRIMITIVES:  return COUNTER_REG(IAPRIMITIVES);
   case PIPE_STAT_QUERY_VS_INVOCATIONS: return COUNTER_REG(VSINVOCATIONS);
   case PIPE_STAT_QUERY_GS_INVOCATIONS: return COUNTER_REG(GSINVOCATIONS);
   case PIPE_STAT_QUERY_GS_PRIMITIVES:  return COUNTER_REG(GSPRIMITIVES);
   case PIPE_STAT_QUERY_C_INVOCATIONS:  return COUNTER_REG(CINVOCATIONS);
   case PIPE_STAT_QUERY_C_PRIMITIVES:   return COUNTER_REG(CPRIMITIVES);
   case PIPE_STAT_QUERY_PS_INVOCATIONS: return COUNTER_REG(PSINVOCATIONS);
   case PIPE_STAT_QUERY_HS_INVOCATIONS: return COUNTER_REG(HSINVOCATIONS);
   case PIPE_STAT_QUERY_DS_INVOCATIONS: return COUNTER_REG(DSINVOCATIONS);
   case PIPE_STAT_QUERY_CS_INVOCATIONS: return COUNTER_REG(CSINVOCATIONS);
   default:
      return 0;
   }
#undef COUNTER_REG
}

template <chip CHIP>
static void
pipeline_stats_resume(struct fd_acc_query *aq, struct fd_batch *batch)
   assert_dt
{
   enum stats_type type = get_stats_type(aq);
   unsigned reg = stats_counter_reg<CHIP>(aq);
   fd_cs cs(batch->draw);

   emit_counter_barrier<CHIP>(cs);

   /* snapshot the start value: */
   fd_pkt7(cs, CP_REG_TO_MEM, 3)
      .add(CP_REG_TO_MEM_0(.reg = reg, .cnt = 2, ._64b = true))
      .add(A5XX_CP_REG_TO_MEM_DEST(stats_sample(aq, start)));

   assert(type < ARRAY_SIZE(batch->pipeline_stats_queries_active));

   if (!batch->pipeline_stats_queries_active[type])
      fd6_event_write<CHIP>(batch->ctx, cs, stats_counter_events[type].start);
   batch->pipeline_stats_queries_active[type]++;
}

template <chip CHIP>
static void
pipeline_stats_pause(struct fd_acc_query *aq, struct fd_batch *batch)
   assert_dt
{
   enum stats_type type = get_stats_type(aq);
   unsigned reg = stats_counter_reg<CHIP>(aq);
   fd_cs cs(batch->draw);

   emit_counter_barrier<CHIP>(cs);

   /* snapshot the end values: */
   fd_pkt7(cs, CP_REG_TO_MEM, 3)
      .add(CP_REG_TO_MEM_0(.reg = reg, .cnt = 2, ._64b = true))
      .add(A5XX_CP_REG_TO_MEM_DEST(stats_sample(aq, stop)));

   assert(type < ARRAY_SIZE(batch->pipeline_stats_queries_active));
   assert(batch->pipeline_stats_queries_active[type] > 0);

   batch->pipeline_stats_queries_active[type]--;
   if (batch->pipeline_stats_queries_active[type])
      fd6_event_write<CHIP>(batch->ctx, cs, stats_counter_events[type].stop);

   /* result += stop - start: */
   fd_pkt7(cs, CP_MEM_TO_MEM, 9)
      .add(CP_MEM_TO_MEM_0(
         .neg_c = true,
         ._double = true,
         .wait_for_mem_writes = true
      ))
      .add(CP_MEM_TO_MEM_DST(stats_sample(aq, result)))
      .add(CP_MEM_TO_MEM_SRC_A(stats_sample(aq, result)))
      .add(CP_MEM_TO_MEM_SRC_B(stats_sample(aq, stop)))
      .add(CP_MEM_TO_MEM_SRC_C(stats_sample(aq, start)));
}

static void
pipeline_stats_result(struct fd_acc_query *aq,
                      struct fd_acc_query_sample *s,
                      union pipe_query_result *result)
{
   struct fd6_pipeline_stats_sample *ps = fd6_pipeline_stats_sample(s);

   result->u64 = ps->result;
}

static void
pipeline_stats_result_resource(struct fd_acc_query *aq,
                               struct fd_ringbuffer *ring,
                               enum pipe_query_value_type result_type,
                               int index, struct fd_resource *dst,
                               unsigned offset)
{
   copy_result(ring, result_type, dst, offset, fd_resource(aq->prsc),
               offsetof(struct fd6_pipeline_stats_sample, result));
}

template <chip CHIP>
static const struct fd_acc_sample_provider primitives_generated = {
   .query_type = PIPE_QUERY_PRIMITIVES_GENERATED,
   .size = sizeof(struct fd6_pipeline_stats_sample),
   .resume = pipeline_stats_resume<CHIP>,
   .pause = pipeline_stats_pause<CHIP>,
   .result = pipeline_stats_result,
   .result_resource = pipeline_stats_result_resource,
};

template <chip CHIP>
static const struct fd_acc_sample_provider pipeline_statistics_single = {
   .query_type = PIPE_QUERY_PIPELINE_STATISTICS_SINGLE,
   .size = sizeof(struct fd6_pipeline_stats_sample),
   .resume = pipeline_stats_resume<CHIP>,
   .pause = pipeline_stats_pause<CHIP>,
   .result = pipeline_stats_result,
   .result_resource = pipeline_stats_result_resource,
};

struct PACKED fd6_primitives_sample {
   struct fd_acc_query_sample base;

   /* VPC_SO_QUERY_BASE dest address must be 32b aligned: */
   uint64_t pad[3];

   struct {
      uint64_t emitted, generated;
   } start[4], stop[4], result;
};
FD_DEFINE_CAST(fd_acc_query_sample, fd6_primitives_sample);

#define primitives_sample(aq, field) \
   fd_resource((aq)->prsc)->bo, __offsetof(struct fd6_primitives_sample, field)

static void
log_primitives_sample(struct fd6_primitives_sample *ps)
{
#ifdef DEBUG_COUNTERS
   mesa_logd("  so counts");
   for (int i = 0; i < ARRAY_SIZE(ps->start); i++) {
      mesa_logd("  CHANNEL %d emitted\t0x%016" PRIx64 "\t0x%016" PRIx64
             "\t%" PRIi64,
             i, ps->start[i].generated, ps->stop[i].generated,
             ps->stop[i].generated - ps->start[i].generated);
      mesa_logd("  CHANNEL %d generated\t0x%016" PRIx64 "\t0x%016" PRIx64
             "\t%" PRIi64,
             i, ps->start[i].emitted, ps->stop[i].emitted,
             ps->stop[i].emitted - ps->start[i].emitted);
   }

   mesa_logd("generated %" PRIu64 ", emitted %" PRIu64, ps->result.generated,
          ps->result.emitted);
#endif
}

template <chip CHIP>
static void
primitives_emitted_resume(struct fd_acc_query *aq,
                          struct fd_batch *batch) assert_dt
{
   fd_cs cs(batch->draw);

   emit_counter_barrier<CHIP>(cs);

   ASSERT_ALIGNED(struct fd6_primitives_sample, start[0], 32);

   fd_pkt4(cs, 2)
      .add(VPC_SO_QUERY_BASE(CHIP, primitives_sample(aq, start[0])));

   fd6_event_write<CHIP>(batch->ctx, cs, FD_WRITE_PRIMITIVE_COUNTS);
}

static void
accumultate_primitives_emitted(struct fd_acc_query *aq, fd_cs &cs, int idx)
{
   /* result += stop - start: */
   fd_pkt7(cs, CP_MEM_TO_MEM, 9)
      .add(CP_MEM_TO_MEM_0(.neg_c = true, ._double = true, .unk31 = true))
      .add(CP_MEM_TO_MEM_DST(primitives_sample(aq, result.emitted)))
      .add(CP_MEM_TO_MEM_SRC_A(primitives_sample(aq, result.emitted)))
      .add(CP_MEM_TO_MEM_SRC_B(primitives_sample(aq, stop[idx].emitted)))
      .add(CP_MEM_TO_MEM_SRC_C(primitives_sample(aq, start[idx].emitted)));
}

static void
accumultate_primitives_generated(struct fd_acc_query *aq, fd_cs &cs, int idx)
{
   /* result += stop - start: */
   fd_pkt7(cs, CP_MEM_TO_MEM, 9)
      .add(CP_MEM_TO_MEM_0(.neg_c = true, ._double = true, .unk31 = true))
      .add(CP_MEM_TO_MEM_DST(primitives_sample(aq, result.generated)))
      .add(CP_MEM_TO_MEM_SRC_A(primitives_sample(aq, result.generated)))
      .add(CP_MEM_TO_MEM_SRC_B(primitives_sample(aq, stop[idx].generated)))
      .add(CP_MEM_TO_MEM_SRC_C(primitives_sample(aq, start[idx].generated)));
}

template <chip CHIP>
static void
primitives_emitted_pause(struct fd_acc_query *aq,
                         struct fd_batch *batch) assert_dt
{
   fd_cs cs(batch->draw);

   emit_counter_barrier<CHIP>(cs);

   ASSERT_ALIGNED(struct fd6_primitives_sample, stop[0], 32);

   fd_pkt4(cs, 2)
      .add(VPC_SO_QUERY_BASE(CHIP, primitives_sample(aq, stop[0])));

   fd6_event_write<CHIP>(batch->ctx, cs, FD_WRITE_PRIMITIVE_COUNTS);
   fd6_event_write<CHIP>(batch->ctx, cs, FD_CACHE_CLEAN);

   if (aq->provider->query_type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      /* Need results from all channels: */
      for (int i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
         accumultate_primitives_emitted(aq, cs, i);
         accumultate_primitives_generated(aq, cs, i);
      }
   } else {
      accumultate_primitives_emitted(aq, cs, aq->base.index);
      /* Only need primitives generated counts for the overflow queries: */
      if (aq->provider->query_type == PIPE_QUERY_SO_OVERFLOW_PREDICATE)
         accumultate_primitives_generated(aq, cs, aq->base.index);
   }
}

static void
primitives_emitted_result(struct fd_acc_query *aq,
                          struct fd_acc_query_sample *s,
                          union pipe_query_result *result)
{
   struct fd6_primitives_sample *ps = fd6_primitives_sample(s);

   log_primitives_sample(ps);

   result->u64 = ps->result.emitted;
}

static void
primitives_emitted_result_resource(struct fd_acc_query *aq,
                                   struct fd_ringbuffer *ring,
                                   enum pipe_query_value_type result_type,
                                   int index, struct fd_resource *dst,
                                   unsigned offset)
{
   copy_result(ring, result_type, dst, offset, fd_resource(aq->prsc),
               offsetof(struct fd6_primitives_sample, result.emitted));
}

static void
so_overflow_predicate_result(struct fd_acc_query *aq,
                             struct fd_acc_query_sample *s,
                             union pipe_query_result *result)
{
   struct fd6_primitives_sample *ps = fd6_primitives_sample(s);

   log_primitives_sample(ps);

   result->b = ps->result.emitted != ps->result.generated;
}

static void
so_overflow_predicate_result_resource(struct fd_acc_query *aq,
                                      struct fd_ringbuffer *ring,
                                      enum pipe_query_value_type result_type,
                                      int index, struct fd_resource *dst,
                                      unsigned offset)
{
   fd_cs cs(ring);

   cs.attach_bo(dst->bo);
   cs.attach_bo(fd_resource(aq->prsc)->bo);

   /* result = generated - emitted: */
   fd_pkt7(cs, CP_MEM_TO_MEM, 7)
      .add(CP_MEM_TO_MEM_0(
         .neg_b = true,
         ._double = result_type >= PIPE_QUERY_TYPE_I64,
      ))
      .add(CP_MEM_TO_MEM_DST(dst->bo, offset))
      .add(CP_MEM_TO_MEM_SRC_A(primitives_sample(aq, result.generated)))
      .add(CP_MEM_TO_MEM_SRC_B(primitives_sample(aq, result.emitted)));

   /* This is a bit awkward, but glcts expects the result to be 1 or 0
    * rather than non-zero vs zero:
    */
   fd_pkt7(cs, CP_COND_WRITE5, 9)
      .add(CP_COND_WRITE5_0(
         .function = WRITE_NE,
         .poll = POLL_MEMORY,
         .write_memory = true
      ))
      .add(CP_COND_WRITE5_POLL_ADDR(dst->bo, offset))
      .add(CP_COND_WRITE5_3(.ref = 0))
      .add(CP_COND_WRITE5_4(.mask = ~0))
      .add(CP_COND_WRITE5_WRITE_ADDR(dst->bo, offset))
      .add(1)
      .add(0);
}

template <chip CHIP>
static const struct fd_acc_sample_provider primitives_emitted = {
   .query_type = PIPE_QUERY_PRIMITIVES_EMITTED,
   .size = sizeof(struct fd6_primitives_sample),
   .resume = primitives_emitted_resume<CHIP>,
   .pause = primitives_emitted_pause<CHIP>,
   .result = primitives_emitted_result,
   .result_resource = primitives_emitted_result_resource,
};

template <chip CHIP>
static const struct fd_acc_sample_provider so_overflow_any_predicate = {
   .query_type = PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE,
   .size = sizeof(struct fd6_primitives_sample),
   .resume = primitives_emitted_resume<CHIP>,
   .pause = primitives_emitted_pause<CHIP>,
   .result = so_overflow_predicate_result,
   .result_resource = so_overflow_predicate_result_resource,
};

template <chip CHIP>
static const struct fd_acc_sample_provider so_overflow_predicate = {
   .query_type = PIPE_QUERY_SO_OVERFLOW_PREDICATE,
   .size = sizeof(struct fd6_primitives_sample),
   .resume = primitives_emitted_resume<CHIP>,
   .pause = primitives_emitted_pause<CHIP>,
   .result = so_overflow_predicate_result,
   .result_resource = so_overflow_predicate_result_resource,
};

/*
 * Performance Counter (batch) queries:
 *
 * Only one of these is active at a time, per design of the gallium
 * batch_query API design.  On perfcntr query tracks N query_types,
 * each of which has a 'fd_batch_query_entry' that maps it back to
 * the associated group and counter.
 */

struct fd_batch_query_entry {
   uint8_t gid; /* group-id */
   uint8_t cid; /* countable-id within the group */
};

struct fd_batch_query_data {
   struct fd_screen *screen;
   unsigned num_query_entries;
   struct fd_batch_query_entry query_entries[];
};

static void
perfcntr_resume(struct fd_acc_query *aq, struct fd_batch *batch) assert_dt
{
   struct fd_batch_query_data *data = (struct fd_batch_query_data *)aq->query_data;
   struct fd_screen *screen = data->screen;
   fd_cs cs(batch->draw);

   unsigned counters_per_group[screen->num_perfcntr_groups];
   memset(counters_per_group, 0, sizeof(counters_per_group));

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   /* configure performance counters for the requested queries: */
   for (unsigned i = 0; i < data->num_query_entries; i++) {
      struct fd_batch_query_entry *entry = &data->query_entries[i];
      const struct fd_perfcntr_group *g = &screen->perfcntr_groups[entry->gid];
      unsigned counter_idx = counters_per_group[entry->gid]++;

      assert(counter_idx < g->num_counters);

      fd_pkt4(cs, 1).add((fd_reg_pair){
         .reg = g->counters[counter_idx].select_reg,
         .value = g->countables[entry->cid].selector,
      });
   }

   memset(counters_per_group, 0, sizeof(counters_per_group));

   /* and snapshot the start values */
   for (unsigned i = 0; i < data->num_query_entries; i++) {
      struct fd_batch_query_entry *entry = &data->query_entries[i];
      const struct fd_perfcntr_group *g = &screen->perfcntr_groups[entry->gid];
      unsigned counter_idx = counters_per_group[entry->gid]++;
      const struct fd_perfcntr_counter *counter = &g->counters[counter_idx];

      fd_pkt7(cs, CP_REG_TO_MEM, 3)
         .add(CP_REG_TO_MEM_0(.reg = counter->counter_reg_lo, ._64b = true))
         .add(A5XX_CP_REG_TO_MEM_DEST(query_sample_idx(aq, i, start)));
   }
}

static void
perfcntr_pause(struct fd_acc_query *aq, struct fd_batch *batch) assert_dt
{
   struct fd_batch_query_data *data = (struct fd_batch_query_data *)aq->query_data;
   struct fd_screen *screen = data->screen;
   fd_cs cs(batch->draw);

   unsigned counters_per_group[screen->num_perfcntr_groups];
   memset(counters_per_group, 0, sizeof(counters_per_group));

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   /* TODO do we need to bother to turn anything off? */

   /* snapshot the end values: */
   for (unsigned i = 0; i < data->num_query_entries; i++) {
      struct fd_batch_query_entry *entry = &data->query_entries[i];
      const struct fd_perfcntr_group *g = &screen->perfcntr_groups[entry->gid];
      unsigned counter_idx = counters_per_group[entry->gid]++;
      const struct fd_perfcntr_counter *counter = &g->counters[counter_idx];

      fd_pkt7(cs, CP_REG_TO_MEM, 3)
         .add(CP_REG_TO_MEM_0(.reg = counter->counter_reg_lo, ._64b = true))
         .add(A5XX_CP_REG_TO_MEM_DEST(query_sample_idx(aq, i, stop)));
   }

   /* and compute the result: */
   for (unsigned i = 0; i < data->num_query_entries; i++) {
      /* result += stop - start: */
      fd_pkt7(cs, CP_MEM_TO_MEM, 9)
         .add(CP_MEM_TO_MEM_0(.neg_c = true, ._double = true))
         .add(CP_MEM_TO_MEM_DST(query_sample_idx(aq, i, result)))
         .add(CP_MEM_TO_MEM_SRC_A(query_sample_idx(aq, i, result)))
         .add(CP_MEM_TO_MEM_SRC_B(query_sample_idx(aq, i, stop)))
         .add(CP_MEM_TO_MEM_SRC_C(query_sample_idx(aq, i, start)));
   }
}

static void
perfcntr_accumulate_result(struct fd_acc_query *aq,
                           struct fd_acc_query_sample *s,
                           union pipe_query_result *result)
{
   struct fd_batch_query_data *data =
         (struct fd_batch_query_data *)aq->query_data;
   struct fd6_query_sample *sp = fd6_query_sample(s);

   for (unsigned i = 0; i < data->num_query_entries; i++) {
      result->batch[i].u64 = sp[i].result;
   }
}

static const struct fd_acc_sample_provider perfcntr = {
   .query_type = FD_QUERY_FIRST_PERFCNTR,
   .always = true,
   .resume = perfcntr_resume,
   .pause = perfcntr_pause,
   .result = perfcntr_accumulate_result,
};

static struct pipe_query *
fd6_create_batch_query(struct pipe_context *pctx, unsigned num_queries,
                       unsigned *query_types)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_screen *screen = ctx->screen;
   struct fd_query *q;
   struct fd_acc_query *aq;
   struct fd_batch_query_data *data;

   data = CALLOC_VARIANT_LENGTH_STRUCT(
      fd_batch_query_data, num_queries * sizeof(data->query_entries[0]));

   data->screen = screen;
   data->num_query_entries = num_queries;

   /* validate the requested query_types and ensure we don't try
    * to request more query_types of a given group than we have
    * counters:
    */
   unsigned counters_per_group[screen->num_perfcntr_groups];
   memset(counters_per_group, 0, sizeof(counters_per_group));

   for (unsigned i = 0; i < num_queries; i++) {
      unsigned idx = query_types[i] - FD_QUERY_FIRST_PERFCNTR;

      /* verify valid query_type, ie. is it actually a perfcntr? */
      if ((query_types[i] < FD_QUERY_FIRST_PERFCNTR) ||
          (idx >= screen->num_perfcntr_queries)) {
         mesa_loge("invalid batch query query_type: %u", query_types[i]);
         goto error;
      }

      struct fd_batch_query_entry *entry = &data->query_entries[i];
      struct pipe_driver_query_info *pq = &screen->perfcntr_queries[idx];

      entry->gid = pq->group_id;

      /* the perfcntr_queries[] table flattens all the countables
       * for each group in series, ie:
       *
       *   (G0,C0), .., (G0,Cn), (G1,C0), .., (G1,Cm), ...
       *
       * So to find the countable index just step back through the
       * table to find the first entry with the same group-id.
       */
      while (pq > screen->perfcntr_queries) {
         pq--;
         if (pq->group_id == entry->gid)
            entry->cid++;
      }

      if (counters_per_group[entry->gid] >=
          screen->perfcntr_groups[entry->gid].num_counters) {
         mesa_loge("too many counters for group %u", entry->gid);
         goto error;
      }

      counters_per_group[entry->gid]++;
   }

   q = fd_acc_create_query2(ctx, 0, 0, &perfcntr);
   aq = fd_acc_query(q);

   /* sample buffer size is based on # of queries: */
   aq->size = num_queries * sizeof(struct fd6_query_sample);
   aq->query_data = data;

   return (struct pipe_query *)q;

error:
   free(data);
   return NULL;
}

template <chip CHIP>
void
fd6_query_context_init(struct pipe_context *pctx) disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->create_query = fd_acc_create_query;
   ctx->query_update_batch = fd_acc_query_update_batch;

   ctx->record_timestamp = record_timestamp<CHIP>;
   ctx->ts_to_ns = ticks_to_ns;

   pctx->create_batch_query = fd6_create_batch_query;

   fd_acc_query_register_provider(pctx, &occlusion_counter<CHIP>);
   fd_acc_query_register_provider(pctx, &occlusion_predicate<CHIP>);
   fd_acc_query_register_provider(pctx, &occlusion_predicate_conservative<CHIP>);

   fd_acc_query_register_provider(pctx, &time_elapsed<CHIP>);
   fd_acc_query_register_provider(pctx, &timestamp<CHIP>);

   fd_acc_query_register_provider(pctx, &primitives_generated<CHIP>);
   fd_acc_query_register_provider(pctx, &pipeline_statistics_single<CHIP>);

   fd_acc_query_register_provider(pctx, &primitives_emitted<CHIP>);
   fd_acc_query_register_provider(pctx, &so_overflow_any_predicate<CHIP>);
   fd_acc_query_register_provider(pctx, &so_overflow_predicate<CHIP>);
}
FD_GENX(fd6_query_context_init);
