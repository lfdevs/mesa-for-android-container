/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "util/perf/u_perfetto.h"
#include "util/perf/u_perfetto_renderpass.h"
#include "util/simple_mtx.h"

#include "freedreno_tracepoints.h"

static uint32_t gpu_clock_id;
static uint64_t next_clock_sync_ns; /* cpu time of next clk sync */

/**
 * The timestamp at the point where we first emitted the clock_sync..
 * this  will be a *later* timestamp that the first GPU traces (since
 * we capture the first clock_sync from the CPU *after* the first GPU
 * tracepoints happen).  To avoid confusing perfetto we need to drop
 * the GPU traces with timestamps before this.
 */
static uint64_t sync_gpu_ts;
static simple_mtx_t clock_sync_mtx = SIMPLE_MTX_INITIALIZER;

struct FdRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = MesaRenderpassIncrementalState;
};

class FdRenderpassDataSource : public MesaRenderpassDataSource<FdRenderpassDataSource, FdRenderpassTraits> {
public:

   void OnStart(const StartArgs &args) override
   {
      MesaRenderpassDataSource<FdRenderpassDataSource, FdRenderpassTraits>::OnStart(args);

      /* See: https://perfetto.dev/docs/concepts/clock-sync
       *
       * Use sequence-scoped clock (64 <= ID < 128) for GPU clock because
       * there's no central daemon emitting consistent snapshots for
       * synchronization between CPU and GPU clocks on behalf of renderstages
       * and counters producers.
       *
       * When CPU clock is the same with the authoritative trace clock
       * (normally default to CLOCK_BOOTTIME), perfetto drops the
       * non-monotonic snapshots to ensure validity of the global source clock
       * in the resolution graph. When they are different, the clocks are
       * marked invalid and the rest of the clock syncs will fail during trace
       * processing.
       *
       * Meanwhile, since the clock is now sequence-scoped (unique per
       * producer + writer pair within the tracing session), we can simply
       * pick 64.
       */
      gpu_clock_id = 64;
   }
};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(FdRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(FdRenderpassDataSource);

static void
send_descriptors(FdRenderpassDataSource::TraceContext &ctx, uint64_t ts_ns)
{
   PERFETTO_LOG("Sending renderstage descriptors");

   auto packet = ctx.NewTracePacket();

   packet->set_timestamp(0);
//   packet->set_timestamp(ts_ns);
//   packet->set_timestamp_clock_id(gpu_clock_id);

   auto event = packet->set_gpu_render_stage_event();
   event->set_gpu_id(0);

   auto spec = event->set_specifications();

   for (unsigned i = 0; i < ARRAY_SIZE(queues); i++) {
      auto desc = spec->add_hw_queue();

      desc->set_name(queues[i].name);
      desc->set_description(queues[i].desc);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(stages); i++) {
      auto desc = spec->add_stage();

      desc->set_name(stages[i].name);
      if (stages[i].desc)
         desc->set_description(stages[i].desc);
   }
}

static void
stage_start(struct pipe_context *pctx, uint64_t ts_ns, enum fd_stage_id stage)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_perfetto_state *p = &ctx->perfetto;

   p->start_ts[stage] = ts_ns;
}

static void
sync_timestamp(struct fd_context *ctx)
{
   uint64_t cpu_ts = perfetto::base::GetBootTimeNs().count();
   uint64_t gpu_ts;

   if (!ctx->ts_to_ns)
      return;

   /* optimistically check without lock: */
   if (cpu_ts < next_clock_sync_ns)
      return;

   if (fd_pipe_get_param(ctx->pipe, FD_TIMESTAMP, &gpu_ts)) {
      PERFETTO_ELOG("Could not sync CPU and GPU clocks");
      return;
   }

   simple_mtx_lock(&clock_sync_mtx);

   if (cpu_ts >= next_clock_sync_ns) {
      /* get cpu timestamp again because FD_TIMESTAMP can take >100us */
      uint32_t cpu_clock_id = perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
      cpu_ts = perfetto::base::GetBootTimeNs().count();

      /* convert GPU ts into ns: */
      gpu_ts = ctx->ts_to_ns(gpu_ts);

      FdRenderpassDataSource::Trace([=](auto tctx) {
         MesaRenderpassDataSource<FdRenderpassDataSource,
                                 FdRenderpassTraits>::EmitClockSync(tctx, cpu_ts,
                                                                     gpu_ts, cpu_clock_id,
                                                                     gpu_clock_id);
         sync_gpu_ts = gpu_ts;
         next_clock_sync_ns = cpu_ts + 30000000;
      });
   }

   simple_mtx_unlock(&clock_sync_mtx);
}

static void
stage_end(struct pipe_context *pctx, uint64_t ts_ns, enum fd_stage_id stage)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_perfetto_state *p = &ctx->perfetto;

   sync_timestamp(ctx);

   FdRenderpassDataSource::Trace([=](FdRenderpassDataSource::TraceContext tctx) {
      if (auto state = tctx.GetIncrementalState(); state->was_cleared) {
         send_descriptors(tctx, p->start_ts[stage]);
         state->was_cleared = false;
      }

      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(p->start_ts[stage]);
      packet->set_timestamp_clock_id(gpu_clock_id);

      auto event = packet->set_gpu_render_stage_event();
      event->set_event_id(0); // ???
      event->set_hw_queue_id(DEFAULT_HW_QUEUE_ID);
      event->set_duration(ts_ns - p->start_ts[stage]);
      event->set_stage_id(stage);
      event->set_context((uintptr_t)pctx);

      /* The "surface" meta-stage has extra info about render target: */
      if (stage == SURFACE_STAGE_ID) {

         event->set_submission_id(p->submit_id);

         if (p->cbuf0_format) {
            auto data = event->add_extra_data();

            data->set_name("color0 format");
            data->set_value(util_format_short_name(p->cbuf0_format));
         }

         if (p->zs_format) {
            auto data = event->add_extra_data();

            data->set_name("zs format");
            data->set_value(util_format_short_name(p->zs_format));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("width");
            data->set_value(std::to_string(p->width));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("height");
            data->set_value(std::to_string(p->height));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("MSAA");
            data->set_value(std::to_string(p->samples));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("MRTs");
            data->set_value(std::to_string(p->mrts));
         }

         // "renderMode"
         // "surfaceID"

         if (p->nbins) {
            auto data = event->add_extra_data();

            data->set_name("numberOfBins");
            data->set_value(std::to_string(p->nbins));
         }

         if (p->binw) {
            auto data = event->add_extra_data();

            data->set_name("binWidth");
            data->set_value(std::to_string(p->binw));
         }

         if (p->binh) {
            auto data = event->add_extra_data();

            data->set_name("binHeight");
            data->set_value(std::to_string(p->binh));
         }
      } else if (stage == NONDRAW_STAGE_ID) {
         event->set_submission_id(p->submit_id);
      } else if (stage == COMPUTE_STAGE_ID) {
         {
            auto data = event->add_extra_data();

            data->set_name("indirect");
            data->set_value(std::to_string(p->indirect));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("work_dim");
            data->set_value(std::to_string(p->work_dim));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("local_size_x");
            data->set_value(std::to_string(p->local_size_x));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("local_size_y");
            data->set_value(std::to_string(p->local_size_y));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("local_size_z");
            data->set_value(std::to_string(p->local_size_z));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("num_groups_x");
            data->set_value(std::to_string(p->num_groups_x));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("num_groups_y");
            data->set_value(std::to_string(p->num_groups_y));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("num_groups_z");
            data->set_value(std::to_string(p->num_groups_z));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("shader_id");
            data->set_value(std::to_string(p->shader_id));
         }
      }
   });
}

#ifdef __cplusplus
extern "C" {
#endif

void
fd_perfetto_init(void)
{
   perfetto::DataSourceDescriptor dsd;
#if DETECT_OS_ANDROID
   // Android tooling expects this data source name
   dsd.set_name("gpu.renderstages");
#else
   dsd.set_name("gpu.renderstages.msm");
#endif
   FdRenderpassDataSource::Register(dsd);
}

static void
emit_submit_id(struct fd_context *ctx)
{
   FdRenderpassDataSource::Trace([=](FdRenderpassDataSource::TraceContext tctx) {
      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(perfetto::base::GetBootTimeNs().count());

      auto event = packet->set_vulkan_api_event();
      auto submit = event->set_vk_queue_submit();

      submit->set_submission_id(ctx->submit_count);
   });
}

void
fd_perfetto_submit(struct fd_context *ctx)
{
   /* sync_timestamp isn't free */
   if (!u_trace_perfetto_active(&ctx->trace_context))
      return;

   emit_submit_id(ctx);
}

/*
 * Trace callbacks, called from u_trace once the timestamps from GPU have been
 * collected.
 */

void
fd_start_render_pass(struct pipe_context *pctx, uint64_t ts_ns,
                     uint16_t tp_idx, const void *flush_data,
                     const struct trace_start_render_pass *payload,
                     const void *indirect_data)
{
   stage_start(pctx, ts_ns, SURFACE_STAGE_ID);

   struct fd_perfetto_state *p = &fd_context(pctx)->perfetto;

   p->submit_id = payload->submit_id;
   p->cbuf0_format = payload->cbuf0_format;
   p->zs_format = payload->zs_format;
   p->width = payload->width;
   p->height = payload->height;
   p->mrts = payload->mrts;
   p->samples = payload->samples;
   p->nbins = payload->nbins;
   p->binw = payload->binw;
   p->binh = payload->binh;
}

void
fd_end_render_pass(struct pipe_context *pctx, uint64_t ts_ns,
                   uint16_t tp_idx, const void *flush_data,
                   const struct trace_end_render_pass *payload,
                   const void *indirect_data)
{
   stage_end(pctx, ts_ns, SURFACE_STAGE_ID);
}

void
fd_start_nondraw(struct pipe_context *pctx, uint64_t ts_ns,
                 uint16_t tp_idx, const void *flush_data,
                 const struct trace_start_nondraw *payload,
                 const void *indirect_data)
{
   stage_start(pctx, ts_ns, NONDRAW_STAGE_ID);

   struct fd_perfetto_state *p = &fd_context(pctx)->perfetto;

   p->submit_id = payload->submit_id;
}

void
fd_end_nondraw(struct pipe_context *pctx, uint64_t ts_ns,
               uint16_t tp_idx, const void *flush_data,
               const struct trace_end_nondraw *payload,
               const void *indirect_data)
{
   stage_end(pctx, ts_ns, NONDRAW_STAGE_ID);
}

void
fd_start_binning_ib(struct pipe_context *pctx, uint64_t ts_ns,
                    uint16_t tp_idx, const void *flush_data,
                    const struct trace_start_binning_ib *payload,
                    const void *indirect_data)
{
   stage_start(pctx, ts_ns, BINNING_STAGE_ID);
}

void
fd_end_binning_ib(struct pipe_context *pctx, uint64_t ts_ns,
                  uint16_t tp_idx, const void *flush_data,
                  const struct trace_end_binning_ib *payload,
                  const void *indirect_data)
{
   stage_end(pctx, ts_ns, BINNING_STAGE_ID);
}

void
fd_start_draw_ib(struct pipe_context *pctx, uint64_t ts_ns,
                 uint16_t tp_idx, const void *flush_data,
                 const struct trace_start_draw_ib *payload,
                 const void *indirect_data)
{
   stage_start(
      pctx, ts_ns,
      fd_context(pctx)->perfetto.nbins ? GMEM_STAGE_ID : BYPASS_STAGE_ID);
}

void
fd_end_draw_ib(struct pipe_context *pctx, uint64_t ts_ns,
               uint16_t tp_idx, const void *flush_data,
               const struct trace_end_draw_ib *payload,
               const void *indirect_data)
{
   stage_end(
      pctx, ts_ns,
      fd_context(pctx)->perfetto.nbins ? GMEM_STAGE_ID : BYPASS_STAGE_ID);
}

void
fd_start_blit(struct pipe_context *pctx, uint64_t ts_ns,
              uint16_t tp_idx, const void *flush_data,
              const struct trace_start_blit *payload,
              const void *indirect_data)
{
   stage_start(pctx, ts_ns, BLIT_STAGE_ID);
}

void
fd_end_blit(struct pipe_context *pctx, uint64_t ts_ns,
            uint16_t tp_idx, const void *flush_data,
            const struct trace_end_blit *payload,
            const void *indirect_data)
{
   stage_end(pctx, ts_ns, BLIT_STAGE_ID);
}

void
fd_start_compute(struct pipe_context *pctx, uint64_t ts_ns,
                 uint16_t tp_idx, const void *flush_data,
                 const struct trace_start_compute *payload,
                 const void *indirect_data)
{
   stage_start(pctx, ts_ns, COMPUTE_STAGE_ID);

   struct fd_perfetto_state *p = &fd_context(pctx)->perfetto;

   p->indirect = payload->indirect;
   p->work_dim = payload->work_dim;
   p->local_size_x = payload->local_size_x;
   p->local_size_y = payload->local_size_y;
   p->local_size_z = payload->local_size_z;
   p->num_groups_x = payload->num_groups_x;
   p->num_groups_y = payload->num_groups_y;
   p->num_groups_z = payload->num_groups_z;
   p->shader_id    = payload->shader_id;
}

void
fd_end_compute(struct pipe_context *pctx, uint64_t ts_ns,
               uint16_t tp_idx, const void *flush_data,
               const struct trace_end_compute *payload,
               const void *indirect_data)
{
   stage_end(pctx, ts_ns, COMPUTE_STAGE_ID);
}

void
fd_start_clears(struct pipe_context *pctx, uint64_t ts_ns,
                uint16_t tp_idx, const void *flush_data,
                const struct trace_start_clears *payload,
                const void *indirect_data)
{
   stage_start(pctx, ts_ns, CLEAR_STAGE_ID);
}

void
fd_end_clears(struct pipe_context *pctx, uint64_t ts_ns,
              uint16_t tp_idx, const void *flush_data,
              const struct trace_end_clears *payload,
              const void *indirect_data)
{
   stage_end(pctx, ts_ns, CLEAR_STAGE_ID);
}

void
fd_start_tile_loads(struct pipe_context *pctx, uint64_t ts_ns,
                    uint16_t tp_idx, const void *flush_data,
                    const struct trace_start_tile_loads *payload,
                    const void *indirect_data)
{
   stage_start(pctx, ts_ns, TILE_LOAD_STAGE_ID);
}

void
fd_end_tile_loads(struct pipe_context *pctx, uint64_t ts_ns,
                  uint16_t tp_idx, const void *flush_data,
                  const struct trace_end_tile_loads *payload,
                  const void *indirect_data)
{
   stage_end(pctx, ts_ns, TILE_LOAD_STAGE_ID);
}

void
fd_start_tile_stores(struct pipe_context *pctx, uint64_t ts_ns,
                     uint16_t tp_idx, const void *flush_data,
                     const struct trace_start_tile_stores *payload,
                     const void *indirect_data)
{
   stage_start(pctx, ts_ns, TILE_STORE_STAGE_ID);
}

void
fd_end_tile_stores(struct pipe_context *pctx, uint64_t ts_ns,
                   uint16_t tp_idx, const void *flush_data,
                   const struct trace_end_tile_stores *payload,
                   const void *indirect_data)
{
   stage_end(pctx, ts_ns, TILE_STORE_STAGE_ID);
}

void
fd_start_state_restore(struct pipe_context *pctx, uint64_t ts_ns,
                       uint16_t tp_idx, const void *flush_data,
                       const struct trace_start_state_restore *payload,
                       const void *indirect_data)
{
   stage_start(pctx, ts_ns, STATE_RESTORE_STAGE_ID);
}

void
fd_end_state_restore(struct pipe_context *pctx, uint64_t ts_ns,
                     uint16_t tp_idx, const void *flush_data,
                     const struct trace_end_state_restore *payload,
                     const void *indirect_data)
{
   stage_end(pctx, ts_ns, STATE_RESTORE_STAGE_ID);
}

void
fd_start_vsc_overflow_test(struct pipe_context *pctx, uint64_t ts_ns,
                           uint16_t tp_idx, const void *flush_data,
                           const struct trace_start_vsc_overflow_test *payload,
                           const void *indirect_data)
{
   stage_start(pctx, ts_ns, VSC_OVERFLOW_STAGE_ID);
}

void
fd_end_vsc_overflow_test(struct pipe_context *pctx, uint64_t ts_ns,
                         uint16_t tp_idx, const void *flush_data,
                         const struct trace_end_vsc_overflow_test *payload,
                         const void *indirect_data)
{
   stage_end(pctx, ts_ns, VSC_OVERFLOW_STAGE_ID);
}

void
fd_start_prologue(struct pipe_context *pctx, uint64_t ts_ns,
                  uint16_t tp_idx, const void *flush_data,
                  const struct trace_start_prologue *payload,
                  const void *indirect_data)
{
   stage_start(pctx, ts_ns, PROLOGUE_STAGE_ID);
}

void
fd_end_prologue(struct pipe_context *pctx, uint64_t ts_ns,
                uint16_t tp_idx, const void *flush_data,
                const struct trace_end_prologue *payload,
                const void *indirect_data)
{
   stage_end(pctx, ts_ns, PROLOGUE_STAGE_ID);
}

#ifdef __cplusplus
}
#endif
