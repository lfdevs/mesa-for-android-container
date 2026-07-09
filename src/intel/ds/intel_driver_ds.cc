/*
 * Copyright © 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string>

#include "common/intel_gem.h"
#include "perf/intel_perf.h"

#include "util/hash_table.h"
#include "util/u_process.h"

#include "intel_driver_ds.h"
#include "intel_pps_priv.h"
#include "intel_tracepoints.h"

#ifdef HAVE_PERFETTO

#include "util/perf/u_perfetto.h"
#include "util/perf/u_perfetto_renderpass.h"

#include "intel_tracepoints_perfetto.h"

/* Just naming stages */
static const struct {
   const char *name;

   /* The perfetto UI requires that there is a parent-child relationship
    * within a row of elements. Which means that all children elements must
    * end within the lifespan of their parent.
    *
    * Some elements like stalls and command buffers follow that relationship,
    * but not all. This tells us in which UI row the elements should live.
    */
   enum intel_ds_queue_stage draw_stage;
} intel_queue_stage_desc[INTEL_DS_QUEUE_STAGE_N_STAGES] = {
   /* Order must match the enum! */
   {
      "queue",
      INTEL_DS_QUEUE_STAGE_QUEUE,
   },
   {
      "frame",
      INTEL_DS_QUEUE_STAGE_FRAME,
   },
   {
      "cmd-buffer",
      INTEL_DS_QUEUE_STAGE_CMD_BUFFER,
   },
   {
      "internal-ops",
      INTEL_DS_QUEUE_STAGE_INTERNAL_OPS,
   },
   {
      "stall",
      INTEL_DS_QUEUE_STAGE_STALL,
   },
   {
      "compute",
      INTEL_DS_QUEUE_STAGE_COMPUTE,
   },
   {
      "as-build",
      INTEL_DS_QUEUE_STAGE_AS,
   },
   {
      "RT",
      INTEL_DS_QUEUE_STAGE_RT,
   },
   {
      "render-pass",
      INTEL_DS_QUEUE_STAGE_RENDER_PASS,
   },
   {
      "blorp",
      INTEL_DS_QUEUE_STAGE_BLORP,
   },
   {
      "draw",
      INTEL_DS_QUEUE_STAGE_DRAW,
   },
   {
      "draw_mesh",
      INTEL_DS_QUEUE_STAGE_DRAW_MESH,
   },
};

struct IntelRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = MesaRenderpassIncrementalState;
};

class IntelRenderpassDataSource : public MesaRenderpassDataSource<IntelRenderpassDataSource,
                                                                  IntelRenderpassTraits> {
public:
   /* Make sure we're not losing traces due to lack of shared memory space */
   constexpr static perfetto::BufferExhaustedPolicy kBufferExhaustedPolicy =
      perfetto::BufferExhaustedPolicy::kDrop;
};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(IntelRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(IntelRenderpassDataSource);

using perfetto::protos::pbzero::InternedGpuRenderStageSpecification_RenderStageCategory;

static void
sync_timestamp(IntelRenderpassDataSource::TraceContext &ctx,
               struct intel_ds_device *device)
{
   uint64_t cpu_ts, gpu_ts;
   uint64_t boottime = perfetto::base::GetBootTimeNs().count();

   if (boottime < device->next_clock_sync_ns)
      return;

   if (!intel_gem_read_correlate_cpu_gpu_timestamp(device->fd,
                                                   device->info.kmd_type,
                                                   INTEL_ENGINE_CLASS_RENDER, 0,
                                                   CLOCK_BOOTTIME,
                                                   &cpu_ts, &gpu_ts, NULL)) {
      cpu_ts = perfetto::base::GetBootTimeNs().count();
      intel_gem_read_render_timestamp(device->fd, device->info.kmd_type,
                                      &gpu_ts);
   }

   uint32_t cpu_clock_id = perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
   gpu_ts = intel_device_info_timebase_scale(&device->info, gpu_ts);

   PERFETTO_LOG("sending clocks gpu=0x%08x", device->gpu_clock_id);

   device->next_clock_sync_ns = boottime + 1000000000ull;

   MesaRenderpassDataSource<IntelRenderpassDataSource, IntelRenderpassTraits>::EmitClockSync(ctx,
      cpu_ts, gpu_ts, cpu_clock_id, device->gpu_clock_id);
}

static void
setup_incremental_state(IntelRenderpassDataSource::TraceContext &ctx,
                        struct intel_ds_device *device)
{
   auto state = ctx.GetIncrementalState();
   if (!state->was_cleared)
      return;

   state->was_cleared = false;

   PERFETTO_LOG("Sending renderstage descriptors");

   device->event_id = 0;
   list_for_each_entry_safe(struct intel_ds_queue, queue, &device->queues, link) {
      for (uint32_t s = 0; s < ARRAY_SIZE(queue->stages); s++) {
         queue->stages[s].start_ns[0] = 0;
      }
   }

   {
      auto packet = ctx.NewTracePacket();

      packet->set_timestamp(perfetto::base::GetBootTimeNs().count());
      packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
      packet->set_sequence_flags(perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);

      auto interned_data = packet->set_interned_data();

      {
         auto desc = interned_data->add_graphics_contexts();
         desc->set_iid(device->iid);
         desc->set_pid(getpid());
         switch (device->api) {
         case INTEL_DS_API_OPENGL:
            desc->set_api(perfetto::protos::pbzero::InternedGraphicsContext_Api::OPEN_GL);
            break;
         case INTEL_DS_API_VULKAN:
            desc->set_api(perfetto::protos::pbzero::InternedGraphicsContext_Api::VULKAN);
            break;
         default:
            break;
         }
      }

      /* Emit all the IID picked at device/queue creation. */
      list_for_each_entry_safe(struct intel_ds_queue, queue, &device->queues, link) {
         for (unsigned s = 0; s < INTEL_DS_QUEUE_STAGE_N_STAGES; s++) {
            {
               /* We put the stage number in there so that all rows are order
                * by intel_ds_queue_stage.
                */
               char name[100];
               snprintf(name, sizeof(name), "%.10s-%s-%02u-%s",
                        util_get_process_name(),
                        queue->name, s, intel_queue_stage_desc[s].name);

               auto desc = interned_data->add_gpu_specifications();
               desc->set_iid(queue->stages[s].queue_iid);
               desc->set_name(name);
            }
         }
      }

      for (unsigned i = 0; i < ARRAY_SIZE(intel_tracepoint_names); i++) {
         /* Skip the begin tracepoint, the label represent the couple of
          * begin/end tracepoints.
          */
         if (strstr(intel_tracepoint_names[i], "intel_begin_") != NULL)
            continue;

         auto desc = interned_data->add_gpu_specifications();
         desc->set_iid(device->tracepoint_iids[i]);
         desc->set_name(intel_tracepoint_names[i] + strlen("intel_end_"));
      }
   }

   device->next_clock_sync_ns = 0;
}

static void
begin_event(struct intel_ds_queue *queue, uint64_t ts_ns,
            enum intel_ds_queue_stage stage_id)
{
   uint32_t level = queue->stages[stage_id].level;

   if (level >= (ARRAY_SIZE(queue->stages[stage_id].start_ns) - 1))
      return;

   queue->stages[stage_id].start_ns[level] = ts_ns;
   queue->stages[stage_id].level++;
}

static void
end_event(struct intel_ds_queue *queue, uint64_t ts_ns,
          enum intel_ds_queue_stage stage_id,
          uint32_t submission_id,
          uint16_t tracepoint_idx,
          const char *app_event,
          const void *payload = nullptr,
          const void *indirect_data = nullptr,
          trace_payload_as_extra_func payload_as_extra = nullptr)
{
   struct intel_ds_device *device = queue->device;

   if (queue->stages[stage_id].level == 0)
      return;

   uint32_t level = --queue->stages[stage_id].level;
   struct intel_ds_stage *stage = &queue->stages[stage_id];
   uint64_t start_ns = stage->start_ns[level];

   if (!start_ns)
      return;

   IntelRenderpassDataSource::Trace([=](IntelRenderpassDataSource::TraceContext tctx) {
      setup_incremental_state(tctx, device);

      sync_timestamp(tctx, device);

      uint64_t evt_id = device->event_id++;

      /* If this is an application event, we might need to generate a new
       * stage_iid if not already seen. Otherwise, it's a driver event and we
       * have use the internal stage_iid.
       */
      uint64_t stage_iid = app_event ?
         tctx.GetDataSourceLocked()->debug_marker_stage(tctx, app_event) :
         device->tracepoint_iids[tracepoint_idx];

      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(start_ns);
      packet->set_timestamp_clock_id(device->gpu_clock_id);

      assert(ts_ns >= start_ns);

      auto event = packet->set_gpu_render_stage_event();
      event->set_gpu_id(device->gpu_id);

      event->set_hw_queue_iid(stage->queue_iid);
      event->set_stage_iid(stage_iid);
      event->set_context(device->iid);
      event->set_event_id(evt_id);
      event->set_duration(ts_ns - start_ns);
      event->set_submission_id(submission_id);

      if ((payload || indirect_data) && payload_as_extra) {
         payload_as_extra(event, payload, indirect_data);
      }
   });

   stage->start_ns[level] = 0;
}

/* Variant for dynamic event names (for example, names formatted in
 * stack-local buffers).
 *
 * Takes a std::string by value so the [=] lambda capture copies string storage
 * into the Trace() closure. This avoids dangling pointers when Trace() runs
 * after the caller's stack frame is gone.
 *
 * Keep end_event() with const char* for NULL, string literals, and other
 * long-lived pointers to avoid std::string construction on the common path.
 */
static void
end_event_dyn(struct intel_ds_queue *queue, uint64_t ts_ns,
          enum intel_ds_queue_stage stage_id,
          uint32_t submission_id,
          uint16_t tracepoint_idx,
          std::string app_event,
          const void *payload = nullptr,
          const void *indirect_data = nullptr,
          trace_payload_as_extra_func payload_as_extra = nullptr)
{
   struct intel_ds_device *device = queue->device;

   if (queue->stages[stage_id].level == 0)
      return;

   uint32_t level = --queue->stages[stage_id].level;
   struct intel_ds_stage *stage = &queue->stages[stage_id];
   uint64_t start_ns = stage->start_ns[level];

   if (!start_ns)
      return;

   IntelRenderpassDataSource::Trace([=](IntelRenderpassDataSource::TraceContext tctx) {
      setup_incremental_state(tctx, device);

      sync_timestamp(tctx, device);

      uint64_t evt_id = device->event_id++;

      uint64_t stage_iid = !app_event.empty() ?
         tctx.GetDataSourceLocked()->debug_marker_stage(tctx, app_event.c_str()) :
         device->tracepoint_iids[tracepoint_idx];

      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(start_ns);
      packet->set_timestamp_clock_id(device->gpu_clock_id);

      assert(ts_ns >= start_ns);

      auto event = packet->set_gpu_render_stage_event();
      event->set_gpu_id(device->gpu_id);

      event->set_hw_queue_iid(stage->queue_iid);
      event->set_stage_iid(stage_iid);
      event->set_context(device->iid);
      event->set_event_id(evt_id);
      event->set_duration(ts_ns - start_ns);
      event->set_submission_id(submission_id);

      if ((payload || indirect_data) && payload_as_extra) {
         payload_as_extra(event, payload, indirect_data);
      }
   });

   stage->start_ns[level] = 0;
}

static size_t
snprintf_stages(char *buf, size_t buf_size,
                enum intel_ds_barrier_type type,
                enum intel_ds_stages signal_stages,
                enum intel_ds_stages wait_stages)
{
   return
      snprintf(buf, buf_size, "%s: %s%s%s%s%s%s%s->%s%s%s%s%s%s%s: ",
               type == INTEL_DS_BARRIER_TYPE_IMMEDIATE ? "imm" :
               type == INTEL_DS_BARRIER_TYPE_SIGNAL    ? "signal" :
               type == INTEL_DS_BARRIER_TYPE_WAIT      ? "wait" : "unknown",
               (signal_stages & INTEL_DS_STAGES_TOP_BIT)    ? "+top" : "",
               (signal_stages & INTEL_DS_STAGES_GEOM_BIT)   ? "+geom" : "",
               (signal_stages & INTEL_DS_STAGES_RASTER_BIT) ? "+rast" : "",
               (signal_stages & INTEL_DS_STAGES_DEPTH_BIT)  ? "+ds" : "",
               (signal_stages & INTEL_DS_STAGES_PIXEL_BIT)  ? "+pix" : "",
               (signal_stages & INTEL_DS_STAGES_COLOR_BIT)  ? "+col" : "",
               (signal_stages & INTEL_DS_STAGES_GPGPU_BIT)  ? "+cs" : "",
               (wait_stages & INTEL_DS_STAGES_TOP_BIT)    ? "+top" : "",
               (wait_stages & INTEL_DS_STAGES_GEOM_BIT)   ? "+geom" : "",
               (wait_stages & INTEL_DS_STAGES_RASTER_BIT) ? "+rast" : "",
               (wait_stages & INTEL_DS_STAGES_DEPTH_BIT)  ? "+ds" : "",
               (wait_stages & INTEL_DS_STAGES_PIXEL_BIT)  ? "+pix" : "",
               (wait_stages & INTEL_DS_STAGES_COLOR_BIT)  ? "+col" : "",
               (wait_stages & INTEL_DS_STAGES_GPGPU_BIT)  ? "+cs" : "");
}

static size_t
snprintf_flags(char *buf, size_t buf_size, enum intel_ds_stall_flag bits)
{
   return
      snprintf(buf, buf_size, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
               (bits & INTEL_DS_DEPTH_CACHE_FLUSH_BIT) ? "+depth_flush" : "",
               (bits & INTEL_DS_DATA_CACHE_FLUSH_BIT) ? "+dc_flush" : "",
               (bits & INTEL_DS_HDC_PIPELINE_FLUSH_BIT) ? "+hdc_flush" : "",
               (bits & INTEL_DS_RENDER_TARGET_CACHE_FLUSH_BIT) ? "+rt_flush" : "",
               (bits & INTEL_DS_TILE_CACHE_FLUSH_BIT) ? "+tile_flush" : "",
               (bits & INTEL_DS_L3_FABRIC_FLUSH_BIT) ? "+l3_fabric_flush" : "",
               (bits & INTEL_DS_STATE_CACHE_INVALIDATE_BIT) ? "+state_inv" : "",
               (bits & INTEL_DS_CONST_CACHE_INVALIDATE_BIT) ? "+const_inv" : "",
               (bits & INTEL_DS_VF_CACHE_INVALIDATE_BIT) ? "+vf_inv" : "",
               (bits & INTEL_DS_TEXTURE_CACHE_INVALIDATE_BIT) ? "+tex_inv" : "",
               (bits & INTEL_DS_INST_CACHE_INVALIDATE_BIT) ? "+inst_inv" : "",
               (bits & INTEL_DS_STALL_AT_SCOREBOARD_BIT) ? "+pb_stall" : "",
               (bits & INTEL_DS_DEPTH_STALL_BIT) ? "+depth_stall" : "",
               (bits & INTEL_DS_CS_STALL_BIT) ? "+cs_stall" : "",
               (bits & INTEL_DS_UNTYPED_DATAPORT_CACHE_FLUSH_BIT) ? "+udp_flush" : "",
               (bits & INTEL_DS_END_OF_PIPE_BIT) ? "+eop" : "",
               (bits & INTEL_DS_CCS_CACHE_FLUSH_BIT) ? "+ccs_flush" : "");
}

static size_t
snprintf_reasons(char *buf, size_t buf_size,
                 const char *r1, const char *r2,
                 const char *r3, const char *r4)
{
   return
      snprintf(buf, buf_size, ": %s%s%s%s%s%s%s",
               r1 ? r1 : "unknown",
               r2 ? "; " : "", r2 ? r2 : "",
               r3 ? "; " : "", r3 ? r3 : "",
               r4 ? "; " : "", r4 ? r4 : "");
}

static void
custom_trace_payload_as_extra_end_stall(perfetto::protos::pbzero::GpuRenderStageEvent *event,
                                        const struct trace_intel_end_stall *payload)
{
   char buf[256];
   size_t buf_size = 0;

   {
      auto data = event->add_extra_data();
      data->set_name("reason");

      buf_size += snprintf_flags(buf + buf_size, sizeof(buf) - buf_size,
                                 (enum intel_ds_stall_flag) payload->flags);
      buf_size += snprintf_reasons(buf + buf_size, sizeof(buf) - buf_size,
                                   payload->reason1, payload->reason2,
                                   payload->reason3, payload->reason4);
      assert(strlen(buf) > 0);

      data->set_value(buf);
   }
}

static void
custom_trace_payload_as_extra_end_barrier(perfetto::protos::pbzero::GpuRenderStageEvent *event,
                                          const struct trace_intel_end_barrier *payload)
{
   char buf[256];
   size_t buf_size = 0;

   {
      auto data = event->add_extra_data();
      data->set_name("reason");

      buf_size += snprintf_stages(buf + buf_size, sizeof(buf) - buf_size,
                                  (enum intel_ds_barrier_type) payload->type,
                                  (enum intel_ds_stages) payload->signal_stages,
                                  (enum intel_ds_stages) payload->wait_stages);
      buf_size += snprintf_flags(buf + buf_size, sizeof(buf) - buf_size,
                                 (enum intel_ds_stall_flag) payload->flags);
      buf_size += snprintf_reasons(buf + buf_size, sizeof(buf) - buf_size,
                                   payload->reason1, payload->reason2,
                                   payload->reason3, payload->reason4);
      assert(strlen(buf) > 0);

      data->set_value(buf);
   }
}

#endif /* HAVE_PERFETTO */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_PERFETTO

/*
 * Trace callbacks, called from u_trace once the timestamps from GPU have been
 * collected.
 */

#define CREATE_DUAL_EVENT_CALLBACK(event_name, stage)                   \
   void                                                                 \
   intel_ds_begin_##event_name(struct intel_ds_device *device,          \
                               uint64_t ts_ns,                          \
                               uint16_t tp_idx,                         \
                               const void *flush_data,                  \
                               const struct trace_intel_begin_##event_name *payload, \
                               const void *indirect_data)               \
   {                                                                    \
      const struct intel_ds_flush_data *flush =                         \
         (const struct intel_ds_flush_data *) flush_data;               \
      begin_event(flush->queue, ts_ns, stage);                          \
   }                                                                    \
                                                                        \
   void                                                                 \
   intel_ds_end_##event_name(struct intel_ds_device *device,            \
                             uint64_t ts_ns,                            \
                             uint16_t tp_idx,                           \
                             const void *flush_data,                    \
                             const struct trace_intel_end_##event_name *payload, \
                             const void *indirect_data)                 \
   {                                                                    \
      const struct intel_ds_flush_data *flush =                         \
         (const struct intel_ds_flush_data *) flush_data;               \
      end_event(flush->queue, ts_ns, stage, flush->submission_id,       \
                tp_idx, NULL, payload, indirect_data,                   \
                (trace_payload_as_extra_func)                           \
                &trace_payload_as_extra_intel_end_##event_name);        \
   }                                                                    \

/*
 * Like CREATE_DUAL_EVENT_CALLBACK, but for dynamic event names.
 *
 * The name is formatted with snprintf() into a stack-local buffer, copied into
 * a std::string, and passed by value to end_event_dyn(). That keeps the name
 * alive in the Trace() lambda closure and avoids dangling stack pointers.
 */
#define CREATE_DUAL_EVENT_CALLBACK_DYN(event_name, stage, name_fmt, ...) \
   void                                                                 \
   intel_ds_begin_##event_name(struct intel_ds_device *device,          \
                               uint64_t ts_ns,                          \
                               uint16_t tp_idx,                         \
                               const void *flush_data,                  \
                               const struct trace_intel_begin_##event_name *payload, \
                               const void *indirect_data)               \
   {                                                                    \
      const struct intel_ds_flush_data *flush =                         \
         (const struct intel_ds_flush_data *) flush_data;               \
      begin_event(flush->queue, ts_ns, stage);                          \
   }                                                                    \
                                                                        \
   void                                                                 \
   intel_ds_end_##event_name(struct intel_ds_device *device,            \
                             uint64_t ts_ns,                            \
                             uint16_t tp_idx,                           \
                             const void *flush_data,                    \
                             const struct trace_intel_end_##event_name *payload, \
                             const void *indirect_data)                 \
   {                                                                    \
      const struct intel_ds_flush_data *flush =                         \
         (const struct intel_ds_flush_data *) flush_data;               \
      UNUSED const uint32_t *indirect =                                 \
         (const uint32_t *) indirect_data;                              \
      char buf[64];                                                     \
      std::string name;                                                 \
      if ((name_fmt) != NULL) {                                         \
         snprintf(buf, sizeof(buf), (name_fmt), ##__VA_ARGS__);         \
         name = buf;                                                    \
      }                                                                 \
      end_event_dyn(flush->queue, ts_ns, stage, flush->submission_id,   \
                    tp_idx, std::move(name), payload, indirect_data,    \
                    (trace_payload_as_extra_func)                       \
                    &trace_payload_as_extra_intel_end_##event_name);    \
   }                                                                    \

CREATE_DUAL_EVENT_CALLBACK(frame, INTEL_DS_QUEUE_STAGE_FRAME)
CREATE_DUAL_EVENT_CALLBACK(batch, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(cmd_buffer, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(sba, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(btp, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(render_pass, INTEL_DS_QUEUE_STAGE_RENDER_PASS)
CREATE_DUAL_EVENT_CALLBACK(blorp, INTEL_DS_QUEUE_STAGE_BLORP)
CREATE_DUAL_EVENT_CALLBACK_DYN(draw, INTEL_DS_QUEUE_STAGE_DRAW,
                               "draw(%u)", payload->count)
CREATE_DUAL_EVENT_CALLBACK_DYN(draw_indexed, INTEL_DS_QUEUE_STAGE_DRAW,
                               "draw_indexed(%u)", payload->count)
CREATE_DUAL_EVENT_CALLBACK(draw_indexed_multi, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK_DYN(draw_indexed_indirect, INTEL_DS_QUEUE_STAGE_DRAW,
                               (p_atomic_read_relaxed(&device->trace_context.enabled_traces) &
                                U_TRACE_TYPE_INDIRECTS) ?
                               "draw_indexed_indirect(%u)" : "draw_indexed_indirect",
                               payload->draw_count)
CREATE_DUAL_EVENT_CALLBACK(draw_multi, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK_DYN(draw_indirect, INTEL_DS_QUEUE_STAGE_DRAW,
                               (p_atomic_read_relaxed(&device->trace_context.enabled_traces) &
                                U_TRACE_TYPE_INDIRECTS) ?
                               "draw_indirect(%u)" : "draw_indirect",
                               payload->draw_count)
CREATE_DUAL_EVENT_CALLBACK(draw_indirect_count, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indirect_byte_count, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indexed_indirect_count, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_mesh, INTEL_DS_QUEUE_STAGE_DRAW_MESH)
CREATE_DUAL_EVENT_CALLBACK(draw_mesh_indirect, INTEL_DS_QUEUE_STAGE_DRAW_MESH)
CREATE_DUAL_EVENT_CALLBACK(draw_mesh_indirect_count, INTEL_DS_QUEUE_STAGE_DRAW_MESH)
CREATE_DUAL_EVENT_CALLBACK(xfb, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK_DYN(compute, INTEL_DS_QUEUE_STAGE_COMPUTE,
                               payload->group_z != 1 ? "compute(%u,%u,%u)" :
                               payload->group_y != 1 ? "compute(%u,%u)" :
                                                       "compute(%u)",
                               payload->group_x, payload->group_y, payload->group_z)
CREATE_DUAL_EVENT_CALLBACK_DYN(compute_indirect, INTEL_DS_QUEUE_STAGE_COMPUTE,
                               ((p_atomic_read_relaxed(&device->trace_context.enabled_traces) &
                                  U_TRACE_TYPE_INDIRECTS) && indirect) ?
                               (indirect[2] != 1 ? "compute_indirect(%u,%u,%u)" :
                                indirect[1] != 1 ? "compute_indirect(%u,%u)" :
                                                   "compute_indirect(%u)") :
                               "compute_indirect",
                               indirect ? indirect[0] : 0,
                               indirect ? indirect[1] : 0,
                               indirect ? indirect[2] : 0)
CREATE_DUAL_EVENT_CALLBACK(generate_draws, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(generate_cmds_pre, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(generate_cmds_post, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(trace_copy, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(trace_copy_cb, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(query_clear_blorp, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(query_clear_cs, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(query_copy_cs, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(query_copy_shader, INTEL_DS_QUEUE_STAGE_INTERNAL_OPS)
CREATE_DUAL_EVENT_CALLBACK(write_buffer_marker, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(rays, INTEL_DS_QUEUE_STAGE_RT)
CREATE_DUAL_EVENT_CALLBACK(as_build, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_build_leaves, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_morton_generate, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_morton_sort, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_lbvh_main, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_lbvh_generate_ir, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_ploc_build_internal, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_encode, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_init_header, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_update, INTEL_DS_QUEUE_STAGE_AS)
CREATE_DUAL_EVENT_CALLBACK(as_copy, INTEL_DS_QUEUE_STAGE_AS)

void
intel_ds_begin_cmd_buffer_annotation(struct intel_ds_device *device,
                                     uint64_t ts_ns,
                                     uint16_t tp_idx,
                                     const void *flush_data,
                                     const struct trace_intel_begin_cmd_buffer_annotation *payload,
                                     const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   begin_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_CMD_BUFFER);
}

void
intel_ds_end_cmd_buffer_annotation(struct intel_ds_device *device,
                                   uint64_t ts_ns,
                                   uint16_t tp_idx,
                                   const void *flush_data,
                                   const struct trace_intel_end_cmd_buffer_annotation *payload,
                                   const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   end_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_CMD_BUFFER,
             flush->submission_id, tp_idx, payload->str, NULL, NULL, NULL);
}

void
intel_ds_begin_queue_annotation(struct intel_ds_device *device,
                                uint64_t ts_ns,
                                uint16_t tp_idx,
                                const void *flush_data,
                                const struct trace_intel_begin_queue_annotation *payload,
                                const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   begin_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_QUEUE);
}

void
intel_ds_end_queue_annotation(struct intel_ds_device *device,
                              uint64_t ts_ns,
                              uint16_t tp_idx,
                              const void *flush_data,
                              const struct trace_intel_end_queue_annotation *payload,
                              const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   end_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_QUEUE,
             flush->submission_id, tp_idx, payload->str, NULL, NULL, NULL);
}

void
intel_ds_begin_stall(struct intel_ds_device *device,
                     uint64_t ts_ns,
                     uint16_t tp_idx,
                     const void *flush_data,
                     const struct trace_intel_begin_stall *payload,
                     const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   begin_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_STALL);
}

void
intel_ds_end_stall(struct intel_ds_device *device,
                   uint64_t ts_ns,
                   uint16_t tp_idx,
                   const void *flush_data,
                   const struct trace_intel_end_stall *payload,
                   const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   end_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_STALL,
             flush->submission_id, tp_idx, NULL, payload, indirect_data,
             (trace_payload_as_extra_func)custom_trace_payload_as_extra_end_stall);
}

void
intel_ds_begin_barrier(struct intel_ds_device *device,
                       uint64_t ts_ns,
                       uint16_t tp_idx,
                       const void *flush_data,
                       const struct trace_intel_begin_barrier *payload,
                       const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   begin_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_STALL);
}

void
intel_ds_end_barrier(struct intel_ds_device *device,
                     uint64_t ts_ns,
                     uint16_t tp_idx,
                     const void *flush_data,
                     const struct trace_intel_end_barrier *payload,
                     const void *indirect_data)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   end_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_STALL,
             flush->submission_id, tp_idx, NULL, payload, indirect_data,
             (trace_payload_as_extra_func)custom_trace_payload_as_extra_end_barrier);
}

uint64_t
intel_ds_begin_submit(struct intel_ds_queue *queue)
{
   return perfetto::base::GetBootTimeNs().count();
}

void
intel_ds_end_submit(struct intel_ds_queue *queue,
                    uint64_t start_ts)
{
   if (!u_trace_should_process(&queue->device->trace_context))
      return;

   uint64_t end_ts = perfetto::base::GetBootTimeNs().count();
   uint32_t submission_id = queue->submission_id++;

   IntelRenderpassDataSource::Trace([=](IntelRenderpassDataSource::TraceContext tctx) {
      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(start_ts);

      auto event = packet->set_vulkan_api_event();
      auto submit = event->set_vk_queue_submit();

      submit->set_duration_ns(end_ts - start_ts);
      submit->set_vk_queue((uintptr_t) queue);
      submit->set_submission_id(submission_id);
      submit->set_pid(getpid());
      submit->set_tid(gettid());
   });
}

void intel_ds_perfetto_set_debug_utils_object_name(struct intel_ds_device *device,
   const VkDebugUtilsObjectNameInfoEXT *pNameInfo)
{
   IntelRenderpassDataSource::Trace([=](auto tctx) {
      tctx.GetDataSourceLocked()->SetDebugUtilsObjectNameEXT(tctx, pNameInfo);
   });
}

void intel_ds_perfetto_refresh_debug_utils_object_name(struct intel_ds_device *device,
   const struct vk_object_base *object)
{
   IntelRenderpassDataSource::Trace([=](auto tctx) {
      tctx.GetDataSourceLocked()->RefreshSetDebugUtilsObjectNameEXT(tctx, object);
   });
}

#endif /* HAVE_PERFETTO */

static void
intel_driver_ds_init_once(void)
{
#ifdef HAVE_PERFETTO
   perfetto::DataSourceDescriptor dsd;
#if DETECT_OS_ANDROID
   // Android tooling expects this data source name
   dsd.set_name("gpu.renderstages");
#else
   dsd.set_name("gpu.renderstages.intel");
#endif
   IntelRenderpassDataSource::Register(dsd);
#endif
}

static once_flag intel_driver_ds_once_flag = ONCE_FLAG_INIT;
static uint64_t iid = 1;

static uint64_t get_iid()
{
   return iid++;
}

void
intel_driver_ds_init(void)
{
   call_once(&intel_driver_ds_once_flag,
             intel_driver_ds_init_once);
   intel_gpu_tracepoint_config_variable();
}

void
intel_ds_device_init(struct intel_ds_device *device,
                     const struct intel_device_info *devinfo,
                     int drm_fd,
                     uint32_t gpu_id,
                     enum intel_ds_api api)
{
   memset(device, 0, sizeof(*device));

   device->gpu_id = gpu_id;
   device->gpu_clock_id = intel_pps_clock_id(gpu_id);
   device->fd = drm_fd;
   device->info = *devinfo;
   device->iid = get_iid();
   device->api = api;

#ifdef HAVE_PERFETTO
   assert(ARRAY_SIZE(intel_tracepoint_names) < ARRAY_SIZE(device->tracepoint_iids));
   for (unsigned i = 0; i < ARRAY_SIZE(intel_tracepoint_names); i++)
      device->tracepoint_iids[i] = get_iid();
#endif

   list_inithead(&device->queues);
   simple_mtx_init(&device->trace_context_mutex, mtx_plain);
}

void
intel_ds_device_fini(struct intel_ds_device *device)
{
   u_trace_context_fini(&device->trace_context);
   simple_mtx_destroy(&device->trace_context_mutex);
}

struct intel_ds_queue *
intel_ds_device_init_queue(struct intel_ds_device *device,
                           struct intel_ds_queue *queue,
                           const char *fmt_name,
                           ...)
{
   va_list ap;

   memset(queue, 0, sizeof(*queue));

   queue->device = device;

   va_start(ap, fmt_name);
   vsnprintf(queue->name, sizeof(queue->name), fmt_name, ap);
   va_end(ap);

   for (unsigned s = 0; s < INTEL_DS_QUEUE_STAGE_N_STAGES; s++) {
      queue->stages[s].queue_iid = get_iid();
   }

   list_add(&queue->link, &device->queues);

   return queue;
}

void intel_ds_flush_data_init(struct intel_ds_flush_data *data,
                              struct intel_ds_queue *queue,
                              uint64_t submission_id)
{
   memset(data, 0, sizeof(*data));

   data->queue = queue;
   data->submission_id = submission_id;

   u_trace_init(&data->trace, &queue->device->trace_context);
}

void intel_ds_flush_data_fini(struct intel_ds_flush_data *data)
{
   u_trace_fini(&data->trace);
}

void intel_ds_queue_flush_data(struct intel_ds_queue *queue,
                               struct u_trace *ut,
                               struct intel_ds_flush_data *data,
                               uint32_t frame_nr,
                               bool free_data)
{
   simple_mtx_lock(&queue->device->trace_context_mutex);
   u_trace_flush(ut, data, frame_nr, free_data);
   simple_mtx_unlock(&queue->device->trace_context_mutex);
}

void intel_ds_device_process(struct intel_ds_device *device,
                             bool eof)
{
   simple_mtx_lock(&device->trace_context_mutex);
   u_trace_context_process(&device->trace_context, eof);
   simple_mtx_unlock(&device->trace_context_mutex);
}

#ifdef __cplusplus
}
#endif
