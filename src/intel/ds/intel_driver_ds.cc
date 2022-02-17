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

#include "common/intel_gem.h"
#include "perf/intel_perf.h"

#include "util/hash_table.h"
#include "util/u_process.h"

#include "intel_driver_ds.h"
#include "intel_pps_priv.h"
#include "intel_tracepoints.h"

#ifdef HAVE_PERFETTO

#include "util/u_perfetto.h"

#include "intel_tracepoints_perfetto.h"

/* Just naming stages */
static const struct {
   const char *name;

   /* Tells us if a given stage is pipelined. This is used to build stacks of
    * pipelined elements so that the perfetto UI doesn't get confused by elements
    * ending out of order.
    */
   bool pipelined;

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
      "cmd-buffer",
      false,
      INTEL_DS_QUEUE_STAGE_CMD_BUFFER,
   },
   {
      "stall",
      false,
      INTEL_DS_QUEUE_STAGE_STALL,
   },
   {
      "compute",
      true,
      INTEL_DS_QUEUE_STAGE_COMPUTE,
   },
   {
      "render-pass",
      true,
      INTEL_DS_QUEUE_STAGE_RENDER_PASS,
   },
   {
      "blorp",
      true,
      INTEL_DS_QUEUE_STAGE_BLORP,
   },
   {
      "draw",
      true,
      INTEL_DS_QUEUE_STAGE_DRAW,
   },
};

struct IntelRenderpassIncrementalState {
   bool was_cleared = true;
};

struct IntelRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = IntelRenderpassIncrementalState;
};

class IntelRenderpassDataSource : public perfetto::DataSource<IntelRenderpassDataSource,
                                                            IntelRenderpassTraits> {
public:
   void OnSetup(const SetupArgs &) override
   {
      // Use this callback to apply any custom configuration to your data source
      // based on the TraceConfig in SetupArgs.
   }

   void OnStart(const StartArgs &) override
   {
      // This notification can be used to initialize the GPU driver, enable
      // counters, etc. StartArgs will contains the DataSourceDescriptor,
      // which can be extended.
      u_trace_perfetto_start();
      PERFETTO_LOG("Tracing started");
   }

   void OnStop(const StopArgs &) override
   {
      PERFETTO_LOG("Tracing stopped");

      // Undo any initialization done in OnStart.
      u_trace_perfetto_stop();
      // TODO we should perhaps block until queued traces are flushed?

      Trace([](IntelRenderpassDataSource::TraceContext ctx) {
         auto packet = ctx.NewTracePacket();
         packet->Finalize();
         ctx.Flush();
      });
   }
};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(IntelRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(IntelRenderpassDataSource);

using perfetto::protos::pbzero::InternedGpuRenderStageSpecification_RenderStageCategory;

enum InternedGpuRenderStageSpecification_RenderStageCategory
i915_engine_class_to_category(enum drm_i915_gem_engine_class engine_class)
{
   switch (engine_class) {
   case I915_ENGINE_CLASS_RENDER:
      return InternedGpuRenderStageSpecification_RenderStageCategory::
         InternedGpuRenderStageSpecification_RenderStageCategory_GRAPHICS;
   default:
      return InternedGpuRenderStageSpecification_RenderStageCategory::InternedGpuRenderStageSpecification_RenderStageCategory_OTHER;
   }
}

static void
sync_timestamp(IntelRenderpassDataSource::TraceContext &ctx,
               struct intel_ds_device *device)
{
   uint64_t cpu_ts = perfetto::base::GetBootTimeNs().count();
   uint64_t gpu_ts = intel_device_info_timebase_scale(&device->info,
                                                      intel_read_gpu_timestamp(device->fd));

   if (cpu_ts < device->next_clock_sync_ns)
      return;

   PERFETTO_LOG("sending clocks gpu=0x%08x", device->gpu_clock_id);

   device->sync_gpu_ts = gpu_ts;
   device->next_clock_sync_ns = cpu_ts + 1000000000ull;

   auto packet = ctx.NewTracePacket();

   packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
   packet->set_timestamp(cpu_ts);

   auto event = packet->set_clock_snapshot();
   {
      auto clock = event->add_clocks();

      clock->set_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
      clock->set_timestamp(cpu_ts);
   }
   {
      auto clock = event->add_clocks();

      clock->set_clock_id(device->gpu_clock_id);
      clock->set_timestamp(gpu_ts);
   }
}

static void
send_descriptors(IntelRenderpassDataSource::TraceContext &ctx,
                 struct intel_ds_device *device)
{
   struct intel_ds_queue *queue;

   PERFETTO_LOG("Sending renderstage descriptors");

   device->event_id = 0;
   u_vector_foreach(queue, &device->queues) {
      for (uint32_t s = 0; s < ARRAY_SIZE(queue->stages); s++) {
         queue->stages[s].start_ns = 0;
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
            desc->set_api(perfetto::protos::pbzero::InternedGraphicsContext_Api_OPEN_GL);
            break;
         case INTEL_DS_API_VULKAN:
            desc->set_api(perfetto::protos::pbzero::InternedGraphicsContext_Api_VULKAN);
            break;
         default:
            break;
         }
      }

      /* Emit all the IID picked at device/queue creation. */
      u_vector_foreach(queue, &device->queues) {
         for (unsigned s = 0; s < INTEL_DS_QUEUE_STAGE_N_STAGES; s++) {
            {
               /* We put the stage number in there so that all rows are order
                * by intel_ds_queue_stage.
                */
               char name[100];
               snprintf(name, sizeof(name), "%.10s-%s-%u-%s",
                        util_get_process_name(),
                        queue->name, s, intel_queue_stage_desc[s].name);

               auto desc = interned_data->add_gpu_specifications();
               desc->set_iid(queue->stages[s].queue_iid);
               desc->set_name(name);
            }
            {
               auto desc = interned_data->add_gpu_specifications();
               desc->set_iid(queue->stages[s].stage_iid);
               desc->set_name(intel_queue_stage_desc[s].name);
            }
         }
      }
   }

   device->next_clock_sync_ns = 0;
   sync_timestamp(ctx, device);
}

typedef void (*trace_payload_as_extra_func)(perfetto::protos::pbzero::GpuRenderStageEvent *, const void*);

static void
begin_event(struct intel_ds_queue *queue, uint64_t ts_ns,
            enum intel_ds_queue_stage stage_id)
{
   /* If we haven't managed to calibrate the alignment between GPU and CPU
    * timestamps yet, then skip this trace, otherwise perfetto won't know
    * what to do with it.
    */
   if (!queue->device->sync_gpu_ts) {
      queue->stages[stage_id].start_ns = 0;
      return;
   }

   queue->stages[stage_id].start_ns = ts_ns;
}

static void
end_event(struct intel_ds_queue *queue, uint64_t ts_ns,
          enum intel_ds_queue_stage stage_id,
          uint32_t submission_id, const void* payload = nullptr,
          trace_payload_as_extra_func payload_as_extra = nullptr)
{
   struct intel_ds_device *device = queue->device;

   /* If we haven't managed to calibrate the alignment between GPU and CPU
    * timestamps yet, then skip this trace, otherwise perfetto won't know
    * what to do with it.
    */
   if (!device->sync_gpu_ts)
      return;

   struct intel_ds_stage *stage = &queue->stages[stage_id];
   uint64_t start_ns = stage->start_ns;

   if (!start_ns)
      return;

   uint64_t evt_id = device->event_id++;

   IntelRenderpassDataSource::Trace([=](IntelRenderpassDataSource::TraceContext tctx) {
      if (auto state = tctx.GetIncrementalState(); state->was_cleared) {
         send_descriptors(tctx, queue->device);
         state->was_cleared = false;
      }

      sync_timestamp(tctx, queue->device);

      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(start_ns);
      packet->set_timestamp_clock_id(queue->device->gpu_clock_id);

      assert(ts_ns >= start_ns);

      auto event = packet->set_gpu_render_stage_event();
      event->set_gpu_id(queue->device->gpu_id);

      event->set_hw_queue_iid(stage->queue_iid);
      event->set_stage_iid(stage->stage_iid);
      event->set_context(queue->device->iid);
      event->set_event_id(evt_id);
      event->set_duration(ts_ns - start_ns);
      event->set_submission_id(submission_id);

      if (payload && payload_as_extra) {
         payload_as_extra(event, payload);
      }
   });

   stage->start_ns = 0;
}

static void
custom_trace_payload_as_extra_end_stall(perfetto::protos::pbzero::GpuRenderStageEvent *event,
                                        const struct trace_intel_end_stall *payload)
{
   char buf[256];

   {
      auto data = event->add_extra_data();
      data->set_name("stall_reason");

      snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s%s%s%s%s%s%s : %s",
              (payload->flags & INTEL_DS_DEPTH_CACHE_FLUSH_BIT) ? "+depth_flush" : "",
              (payload->flags & INTEL_DS_DATA_CACHE_FLUSH_BIT) ? "+dc_flush" : "",
              (payload->flags & INTEL_DS_HDC_PIPELINE_FLUSH_BIT) ? "+hdc_flush" : "",
              (payload->flags & INTEL_DS_RENDER_TARGET_CACHE_FLUSH_BIT) ? "+rt_flush" : "",
              (payload->flags & INTEL_DS_TILE_CACHE_FLUSH_BIT) ? "+tile_flush" : "",
              (payload->flags & INTEL_DS_STATE_CACHE_INVALIDATE_BIT) ? "+state_inv" : "",
              (payload->flags & INTEL_DS_CONST_CACHE_INVALIDATE_BIT) ? "+const_inv" : "",
              (payload->flags & INTEL_DS_VF_CACHE_INVALIDATE_BIT) ? "+vf_inv" : "",
              (payload->flags & INTEL_DS_TEXTURE_CACHE_INVALIDATE_BIT) ? "+tex_inv" : "",
              (payload->flags & INTEL_DS_INST_CACHE_INVALIDATE_BIT) ? "+inst_inv" : "",
              (payload->flags & INTEL_DS_STALL_AT_SCOREBOARD_BIT) ? "+pb_stall" : "",
              (payload->flags & INTEL_DS_DEPTH_STALL_BIT) ? "+depth_stall" : "",
              (payload->flags & INTEL_DS_HDC_PIPELINE_FLUSH_BIT) ? "+hdc_flush" : "",
              (payload->flags & INTEL_DS_CS_STALL_BIT) ? "+cs_stall" : "",
              payload->reason ? payload->reason : "unknown");

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
                               const void *flush_data,                  \
                               const struct trace_intel_begin_##event_name *payload) \
   {                                                                    \
      const struct intel_ds_flush_data *flush =                         \
         (const struct intel_ds_flush_data *) flush_data;               \
      begin_event(flush->queue, ts_ns, stage);                          \
   }                                                                    \
                                                                        \
   void                                                                 \
   intel_ds_end_##event_name(struct intel_ds_device *device,            \
                             uint64_t ts_ns,                            \
                             const void *flush_data,                    \
                             const struct trace_intel_end_##event_name *payload) \
   {                                                                    \
      const struct intel_ds_flush_data *flush =                         \
         (const struct intel_ds_flush_data *) flush_data;               \
      end_event(flush->queue, ts_ns, stage, flush->submission_id,       \
                payload,                                                \
                (trace_payload_as_extra_func)                           \
                &trace_payload_as_extra_intel_end_##event_name);        \
   }                                                                    \


CREATE_DUAL_EVENT_CALLBACK(batch, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(cmd_buffer, INTEL_DS_QUEUE_STAGE_CMD_BUFFER)
CREATE_DUAL_EVENT_CALLBACK(render_pass, INTEL_DS_QUEUE_STAGE_RENDER_PASS)
CREATE_DUAL_EVENT_CALLBACK(blorp, INTEL_DS_QUEUE_STAGE_BLORP)
CREATE_DUAL_EVENT_CALLBACK(draw, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indexed, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indexed_multi, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indexed_indirect, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_multi, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indirect, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indirect_count, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indirect_byte_count, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(draw_indexed_indirect_count, INTEL_DS_QUEUE_STAGE_DRAW)
CREATE_DUAL_EVENT_CALLBACK(compute, INTEL_DS_QUEUE_STAGE_COMPUTE)

void
intel_ds_begin_stall(struct intel_ds_device *device,
                     uint64_t ts_ns,
                     const void *flush_data,
                     const struct trace_intel_begin_stall *payload)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   begin_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_STALL);
}

void
intel_ds_end_stall(struct intel_ds_device *device,
                   uint64_t ts_ns,
                   const void *flush_data,
                   const struct trace_intel_end_stall *payload)
{
   const struct intel_ds_flush_data *flush =
      (const struct intel_ds_flush_data *) flush_data;
   end_event(flush->queue, ts_ns, INTEL_DS_QUEUE_STAGE_STALL, flush->submission_id,
             payload,
             (trace_payload_as_extra_func)custom_trace_payload_as_extra_end_stall);
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
   if (!u_trace_context_actively_tracing(&queue->device->trace_context)) {
      queue->device->sync_gpu_ts = 0;
      queue->device->next_clock_sync_ns = 0;
      return;
   }

   uint64_t end_ts = perfetto::base::GetBootTimeNs().count();
   uint32_t submission_id = queue->submission_id++;

   IntelRenderpassDataSource::Trace([=](IntelRenderpassDataSource::TraceContext tctx) {
      if (auto state = tctx.GetIncrementalState(); state->was_cleared) {
         send_descriptors(tctx, queue->device);
         state->was_cleared = false;
      }

      sync_timestamp(tctx, queue->device);

      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(start_ts);

      auto event = packet->set_vulkan_api_event();
      auto submit = event->set_vk_queue_submit();

      // submit->set_pid(os_get_pid());
      // submit->set_tid(os_get_tid());
      submit->set_duration_ns(end_ts - start_ts);
      submit->set_vk_queue((uintptr_t) queue);
      submit->set_submission_id(submission_id);
   });
}

#endif /* HAVE_PERFETTO */

static void
intel_driver_ds_init_once(void)
{
#ifdef HAVE_PERFETTO
   util_perfetto_init();
   perfetto::DataSourceDescriptor dsd;
   dsd.set_name("gpu.renderstages.intel");
   IntelRenderpassDataSource::Register(dsd);
#endif
}

static once_flag intel_driver_ds_once_flag = ONCE_FLAG_INIT;

static uint64_t get_iid()
{
   static uint64_t iid = 1;
   return iid++;
}

void
intel_driver_ds_init(void)
{
   call_once(&intel_driver_ds_once_flag,
             intel_driver_ds_init_once);
}

void
intel_ds_device_init(struct intel_ds_device *device,
                     struct intel_device_info *devinfo,
                     int drm_fd,
                     uint32_t gpu_id,
                     enum intel_ds_api api)
{
   memset(device, 0, sizeof(*device));

   assert(gpu_id < 128);
   device->gpu_id = gpu_id;
   device->gpu_clock_id = intel_pps_clock_id(gpu_id);
   device->fd = drm_fd;
   device->info = *devinfo;
   device->iid = get_iid();
   device->api = api;
   u_vector_init(&device->queues, 4, sizeof(struct intel_ds_queue));
}

void
intel_ds_device_fini(struct intel_ds_device *device)
{
   u_trace_context_fini(&device->trace_context);
   u_vector_finish(&device->queues);
}

struct intel_ds_queue *
intel_ds_device_add_queue(struct intel_ds_device *device,
                          const char *fmt_name,
                          ...)
{
   struct intel_ds_queue *queue =
      (struct intel_ds_queue *) u_vector_add(&device->queues);
   va_list ap;

   memset(queue, 0, sizeof(*queue));

   queue->device = device;
   queue->queue_id = u_vector_length(&device->queues) - 1;

   va_start(ap, fmt_name);
   vsnprintf(queue->name, sizeof(queue->name), fmt_name, ap);
   va_end(ap);

   for (unsigned s = 0; s < INTEL_DS_QUEUE_STAGE_N_STAGES; s++) {
      queue->stages[s].queue_iid = get_iid();
      queue->stages[s].stage_iid = get_iid();
   }

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

#ifdef __cplusplus
}
#endif
