/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include <functional>
#include <inttypes.h>
#include <perfetto.h>

#include "c11/threads.h"

#include "util/perf/u_perfetto.h"
#include "util/perf/u_perfetto_renderpass.h"

#include "util/format/u_format.h"

#include "panfrost_perfetto.h"
#include "panfrost_tracepoints.h"
#include "panfrost_tracepoints_perfetto.h"

#include "pan_context.h"
#include "pan_device.h"

/*
 * Use sequence-scoped clock (64 <= ID < 128) as panvk. Check
 * panvk_utrace_perfetto.cc, methods get_gpu_clock_id for further details.
 */
static const uint32_t gpu_clock_id = 64;

static const char * const panfrost_stage_names[] = {
   [PANFROST_STAGE_VERTEX_TILER] = "Vertex/Tiler",
   [PANFROST_STAGE_FRAGMENT]     = "Fragment",
   [PANFROST_STAGE_COMPUTE]      = "Compute",
   [PANFROST_STAGE_BATCH]        = "Batch",
   [PANFROST_STAGE_BARRIER]      = "Barrier",
   [PANFROST_STAGE_CACHE_FLUSH]  = "CacheFlush",
};

static const char * const panfrost_queue_names[] = {
   [PANFROST_DEFAULT_HW_QUEUE_ID] = "GPU Queue",
};

/* Interned IDs for queue and stage names; 1-based to avoid iid=0. */
static const uint64_t panfrost_queue_iids[] = {
   [PANFROST_DEFAULT_HW_QUEUE_ID] = 1,
};

static const uint64_t panfrost_stage_iids[] = {
   [PANFROST_STAGE_VERTEX_TILER] = 2,
   [PANFROST_STAGE_FRAGMENT]     = 3,
   [PANFROST_STAGE_COMPUTE]      = 4,
   [PANFROST_STAGE_BATCH]        = 5,
   [PANFROST_STAGE_BARRIER]      = 6,
   [PANFROST_STAGE_CACHE_FLUSH]  = 7,
};

struct PanfrostRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = MesaRenderpassIncrementalState;
};

class PanfrostRenderpassDataSource
    : public MesaRenderpassDataSource<PanfrostRenderpassDataSource,
                                      PanfrostRenderpassTraits> {};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(PanfrostRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(PanfrostRenderpassDataSource);

static void
emit_interned_data_packet(PanfrostRenderpassDataSource::TraceContext &ctx,
                          uint64_t ts_ns)
{
   PERFETTO_LOG("Sending renderstage descriptors");

   auto packet = ctx.NewTracePacket();

   packet->set_timestamp(ts_ns);
   packet->set_sequence_flags(
      perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);

   auto interned_data = packet->set_interned_data();

   for (unsigned i = 0; i < ARRAY_SIZE(panfrost_queue_names); i++) {
      auto spec = interned_data->add_gpu_specifications();
      spec->set_iid(panfrost_queue_iids[i]);
      spec->set_name(panfrost_queue_names[i]);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(panfrost_stage_names); i++) {
      auto spec = interned_data->add_gpu_specifications();
      spec->set_iid(panfrost_stage_iids[i]);
      spec->set_name(panfrost_stage_names[i]);
   }
}

/* Must be called inside a Trace() lambda, as we are using a sequence-scoped
 * GPU clock (ID 64), that is only visible within its own sequence, so a sync
 * from a separate Trace() call would not resolve the timestamps.
 */
static void
sync_timestamp(PanfrostRenderpassDataSource::TraceContext &tctx,
               struct panfrost_context *ctx)
{
   uint64_t cpu_ts = perfetto::base::GetWallTimeRawNs().count();

   if (cpu_ts < ctx->perfetto.next_clock_sync_ns)
      return;

   struct panfrost_device *dev = pan_device(ctx->base.screen);
   uint64_t gpu_ts = pan_kmod_query_timestamp(dev->kmod.dev);

   /* Re-sample CPU time after the query, which can take time. */
   cpu_ts = perfetto::base::GetWallTimeRawNs().count();
   gpu_ts = pan_gpu_time_to_ns(dev, gpu_ts);

   MesaRenderpassDataSource<
      PanfrostRenderpassDataSource,
      PanfrostRenderpassTraits>::EmitClockSync(tctx, cpu_ts, gpu_ts,
         perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW,
         gpu_clock_id);

   ctx->perfetto.next_clock_sync_ns = cpu_ts + NSEC_PER_SEC;
}

static void
stage_start(struct pipe_context *pctx, uint64_t ts_ns,
            enum panfrost_stage_id stage)
{
   struct panfrost_context *ctx = pan_context(pctx);
   struct panfrost_perfetto_state *p = &ctx->perfetto;

   p->start_ts[stage] = ts_ns;
}

static void
stage_end(struct pipe_context *pctx, uint64_t ts_ns,
          enum panfrost_stage_id stage,
          std::function<void(perfetto::protos::pbzero::GpuRenderStageEvent *)>
             emit_extra)
{
   struct panfrost_context *ctx = pan_context(pctx);
   struct panfrost_perfetto_state *p = &ctx->perfetto;

   /* Usually, on the following condition we could skip the event. But for
    * BATCH we must still emit it because Fragment/VT events for this batch
    * have already been emitted and would be isolated without a parent Batch
    * event. Use the cache-flush start (the last fresh timestamp before the
    * stale slots) as a reference end time so the Batch event covers the real
    * GPU work.
    */
   if (p->start_ts[stage] && ts_ns <= p->start_ts[stage]) {
      if (stage != PANFROST_STAGE_BATCH)
         return;
      ts_ns = p->start_ts[PANFROST_STAGE_CACHE_FLUSH]
              ? p->start_ts[PANFROST_STAGE_CACHE_FLUSH]
              : p->start_ts[stage];
   }

   PanfrostRenderpassDataSource::Trace(
      [=](PanfrostRenderpassDataSource::TraceContext tctx) {
         if (auto state = tctx.GetIncrementalState(); state->was_cleared) {
            emit_interned_data_packet(tctx,
               perfetto::base::GetWallTimeRawNs().count());
            state->was_cleared = false;
            /* Force a new clock sync after incremental state is cleared,
             * so the new sequence gets its own clock sync packet before
             * any render stage events.
             */
            p->next_clock_sync_ns = 0;

            /* The GPU clock sequence has been reset.  Any start_ts[BATCH]
             * value left over from a previous session (e.g. because
             * end_batch was skipped when PERFETTO_ACTIVE dropped
             * mid-chunk) would let a batch that is missing its
             * start_batch event bypass the !start_ts[BATCH] guard,
             * producing sub-stage events isolated, without a batch.
             *
             * Clear it now so the guard suppresses such events.  We only do
             * this when processing a sub-stage (stage != BATCH). If
             * was_cleared was set on the BATCH event itself it means
             * VT/Fragment were already emitted in the old sequence, and
             * suppressing Batch as well would make things worse.
             */
            if (stage != PANFROST_STAGE_BATCH)
               p->start_ts[PANFROST_STAGE_BATCH] = 0;
         }

         sync_timestamp(tctx, ctx);

         /* Return if we haven't started to record yet (like if Perfetto
          * became active after csf_init_batch) or if we have an invalid GPU
          * timestamp. Otherwise we get isolated event that should be
          * part of the batch.
          */
         if (!p->start_ts[PANFROST_STAGE_BATCH])
            return;

         auto packet = tctx.NewTracePacket();

         packet->set_timestamp(p->start_ts[stage]);
         packet->set_timestamp_clock_id(gpu_clock_id);

         auto event = packet->set_gpu_render_stage_event();
         event->set_event_id(0);
         event->set_hw_queue_iid(panfrost_queue_iids[PANFROST_DEFAULT_HW_QUEUE_ID]);
         event->set_duration(ts_ns - p->start_ts[stage]);
         event->set_stage_iid(panfrost_stage_iids[stage]);
         event->set_context((uintptr_t)pctx);

         emit_extra(event);
      });

   /* Clear the batch start timestamp after the batch event is emitted so the
    * next batch starts with start_ts[BATCH]=0. We need to do this to avoid
    * sub-stage events to be emitted with the wrong start time if the next
    * Batch misses its start_batch tracepoint.
    */
   if (stage == PANFROST_STAGE_BATCH)
      p->start_ts[PANFROST_STAGE_BATCH] = 0;
}

#ifdef __cplusplus
extern "C" {
#endif

static void
register_data_source(void)
{
   perfetto::DataSourceDescriptor dsd;
#if DETECT_OS_ANDROID
   /* Android tooling expects this data source name */
   dsd.set_name("gpu.renderstages");
#else
   dsd.set_name("gpu.renderstages.panfrost");
#endif
   PanfrostRenderpassDataSource::Register(dsd);
}

void
panfrost_perfetto_init(struct panfrost_device *dev)
{
   /* check for timestamp support */
   if (!dev->kmod.dev->props.gpu_can_query_timestamp ||
       !dev->kmod.dev->props.timestamp_frequency)
      return;

   /* We use CLOCK_MONOTONIC_RAW as panvk. See panvk_utrace_perfetto.cc,
    * methos panvk_utrace_perfetto_init for further details
    */
   util_perfetto_set_default_clock(CLOCK_MONOTONIC_RAW);

   static once_flag register_ds_once = ONCE_FLAG_INIT;
   call_once(&register_ds_once, register_data_source);
}

static void
emit_submit_id(struct panfrost_context *ctx)
{
   PanfrostRenderpassDataSource::Trace(
      [=](PanfrostRenderpassDataSource::TraceContext tctx) {
         auto packet = tctx.NewTracePacket();

         packet->set_timestamp(perfetto::base::GetWallTimeRawNs().count());
         packet->set_timestamp_clock_id(
            perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW);

         /* We use the Vulkan Events to show where submits happen on the
          * CPU. It is useful to be able to connect submits on the CPU to
          * execution on the GPU. This comes from the fact that GL doesn't
          * have a explicit API for this. This workaround has been used for a
          * while, starting with freedreno (see MR#9901)
          */
         auto event = packet->set_vulkan_api_event();
         auto submit = event->set_vk_queue_submit();

         submit->set_submission_id(ctx->submit_count);
      });
}

void
panfrost_perfetto_submit(struct panfrost_context *ctx)
{
   if (!u_trace_perfetto_active(&ctx->trace_context))
      return;

   emit_submit_id(ctx);
}

/*
 * Trace callbacks, called from u_trace once the timestamps from GPU have been
 * collected.  The macro generates a begin/end pair for each tracepoint: the
 * begin stores the GPU start timestamp, and the end emits the Perfetto packet
 * with duration and any per-stage extra data produced by the auto-generated
 * trace_payload_as_extra_end_<tp>() helper.
 */

#define PANFROST_PERFETTO_PROCESS_EVENT(tp, stage)                                   \
   void panfrost_start_##tp(struct pipe_context *pctx, uint64_t ts_ns,              \
                             uint16_t tp_idx, const void *flush_data,                \
                             const struct trace_panfrost_start_##tp *payload,        \
                             const void *indirect_data)                              \
   {                                                                                  \
      stage_start(pctx, ts_ns, PANFROST_STAGE_##stage);                              \
   }                                                                                  \
                                                                                      \
   void panfrost_end_##tp(struct pipe_context *pctx, uint64_t ts_ns,                \
                           uint16_t tp_idx, const void *flush_data,                  \
                           const struct trace_panfrost_end_##tp *payload,            \
                           const void *indirect_data)                                \
   {                                                                                  \
      auto emit_extra =                                                               \
         [=](perfetto::protos::pbzero::GpuRenderStageEvent *event) {                 \
            trace_payload_as_extra_panfrost_end_##tp(event, payload, indirect_data); \
         };                                                                           \
      stage_end(pctx, ts_ns, PANFROST_STAGE_##stage, emit_extra);                    \
   }

PANFROST_PERFETTO_PROCESS_EVENT(vertex_tiler, VERTEX_TILER)
PANFROST_PERFETTO_PROCESS_EVENT(fragment,     FRAGMENT)
PANFROST_PERFETTO_PROCESS_EVENT(compute,          COMPUTE)
PANFROST_PERFETTO_PROCESS_EVENT(compute_indirect, COMPUTE)
PANFROST_PERFETTO_PROCESS_EVENT(batch,        BATCH)
PANFROST_PERFETTO_PROCESS_EVENT(barrier,      BARRIER)
PANFROST_PERFETTO_PROCESS_EVENT(cache_flush,  CACHE_FLUSH)

#ifdef __cplusplus
}
#endif
