/*
 * Copyright © 2020 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "u_trace.h"

#include <inttypes.h>

#include "util/list.h"
#include "util/u_call_once.h"
#include "util/u_debug.h"
#include "util/u_vector.h"

#define __NEEDS_TRACE_PRIV
#include "u_trace_priv.h"

#define TIMESTAMP_BUF_SIZE 0x1000

struct u_trace_state {
   util_once_flag once;
   FILE *trace_file;
   enum u_trace_type enabled_traces;
};
static struct u_trace_state u_trace_state = { .once = UTIL_ONCE_FLAG_INIT };

#ifdef HAVE_PERFETTO
/**
 * Global list of contexts, so we can defer starting the queue until
 * perfetto tracing is started.
 */
static struct list_head ctx_list = { &ctx_list, &ctx_list };

static simple_mtx_t ctx_list_mutex = SIMPLE_MTX_INITIALIZER;
/* The amount of Perfetto tracers connected */
int _u_trace_perfetto_count;
#endif

struct u_trace_event {
   const struct u_tracepoint *tp;
   struct u_trace_buffer_view timestamp;
   struct u_trace_buffer_view indirect;
   uint32_t payload_size;
   alignas(uint64_t) uint8_t payload[];
};

/**
 * A "chunk" of trace-events and corresponding timestamp buffer.  As
 * trace events are emitted, additional trace chucks will be allocated
 * as needed.  When u_trace_flush() is called, they are transferred
 * from the u_trace to the u_trace_context queue.
 */
struct u_trace_flush {
   struct u_trace trace;

   struct util_queue_fence fence;

   bool eof;
   uint32_t frame_nr; /* frame idx from the driver */

   void *flush_data; /* assigned by u_trace_flush */

   /**
    * Several chunks reference a single flush_data instance thus only
    * one chunk should be designated to free the data.
    */
   bool free_flush_data;
};

static void
u_trace_flush_destroy(struct u_trace_flush *flush)
{
   struct u_trace *ut = &flush->trace;
   struct u_trace_context *utctx = ut->utctx;

   if (flush->free_flush_data && utctx->delete_flush_data)
      utctx->delete_flush_data(utctx, flush->flush_data);

   u_trace_fini(ut);

   free(flush);
}

struct u_trace_printer {
   void (*start)(struct u_trace_context *utctx);
   void (*end)(struct u_trace_context *utctx);
   void (*start_of_frame)(struct u_trace_context *utctx);
   void (*end_of_frame)(struct u_trace_context *utctx);
   void (*start_of_batch)(struct u_trace_context *utctx);
   void (*end_of_batch)(struct u_trace_context *utctx);
   void (*event)(struct u_trace_context *utctx,
                 const struct u_trace_event *evt,
                 uint64_t ns,
                 int32_t delta,
                 const void *indirect);
};

static void
print_txt_start(struct u_trace_context *utctx)
{
}

static void
print_txt_end_of_frame(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "END OF FRAME %u\n", utctx->frame_nr);
}

static void
print_txt_start_of_batch(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "+----- NS -----+ +-- Δ --+  +----- MSG -----\n");
}

static void
print_txt_end_of_batch(struct u_trace_context *utctx)
{
   uint64_t elapsed = utctx->last_time_ns - utctx->first_time_ns;
   fprintf(utctx->out, "ELAPSED: %" PRIu64 " ns\n", elapsed);
}

static void
print_txt_event(struct u_trace_context *utctx,
                const struct u_trace_event *evt,
                uint64_t ns,
                int32_t delta,
                const void *indirect)
{
   if (evt->tp->type == u_tracepoint_type_end_range)
      utctx->indentation--;

   fprintf(utctx->out, "%016" PRIu64 " %+9d: ", ns, delta);

   for (uint32_t i = 0; i < utctx->indentation; i++)
      fprintf(utctx->out, "   ");

   fprintf(utctx->out, "%s ", evt->tp->name);

   if (evt->tp->print)
      evt->tp->print(utctx->out, evt->payload, indirect);
   else
      fprintf(utctx->out, "\n");

   if (evt->tp->type == u_tracepoint_type_begin_range)
      utctx->indentation++;
}

static struct u_trace_printer txt_printer = {
   .start = &print_txt_start,
   .end = &print_txt_start,
   .start_of_frame = &print_txt_start,
   .end_of_frame = &print_txt_end_of_frame,
   .start_of_batch = &print_txt_start_of_batch,
   .end_of_batch = &print_txt_end_of_batch,
   .event = &print_txt_event,
};

static void
print_csv_start(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "frame,batch,time_ns,event,\n");
}

static void
print_csv_end(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "\n");
}

static void
print_csv_start_of_frame(struct u_trace_context *utctx)
{
}

static void
print_csv_end_of_frame(struct u_trace_context *utctx)
{
}

static void
print_csv_start_of_batch(struct u_trace_context *utctx)
{
}

static void
print_csv_end_of_batch(struct u_trace_context *utctx)
{
}

static void
print_csv_event(struct u_trace_context *utctx,
                const struct u_trace_event *evt,
                uint64_t ns,
                int32_t delta,
                const void *indirect)
{
   fprintf(utctx->out, "%u,%u,%"PRIu64",%s,",
           utctx->frame_nr, utctx->batch_nr, ns, evt->tp->name);
   if (evt->tp->print) {
      evt->tp->print(utctx->out, evt->payload, indirect);
   } else {
      fprintf(utctx->out, "\n");
   }
}

static struct u_trace_printer csv_printer = {
   .start = print_csv_start,
   .end = print_csv_end,
   .start_of_frame = &print_csv_start_of_frame,
   .end_of_frame = &print_csv_end_of_frame,
   .start_of_batch = &print_csv_start_of_batch,
   .end_of_batch = &print_csv_end_of_batch,
   .event = &print_csv_event,
};

static void
print_json_start(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "[\n");
}

static void
print_json_end(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "\n]");
}

static void
print_json_start_of_frame(struct u_trace_context *utctx)
{
   if (utctx->frame_nr != 0)
      fprintf(utctx->out, ",\n");
   fprintf(utctx->out, "{\n\"frame\": %u,\n", utctx->frame_nr);
   fprintf(utctx->out, "\"batches\": [\n");
}

static void
print_json_end_of_frame(struct u_trace_context *utctx)
{
   fprintf(utctx->out, "]\n}\n");
   fflush(utctx->out);
}

static void
print_json_start_of_batch(struct u_trace_context *utctx)
{
   if (utctx->batch_nr != 0)
      fprintf(utctx->out, ",\n");
   fprintf(utctx->out, "{\n\"events\": [\n");
}

static void
print_json_end_of_batch(struct u_trace_context *utctx)
{
   uint64_t elapsed = utctx->last_time_ns - utctx->first_time_ns;
   fprintf(utctx->out, "],\n");
   fprintf(utctx->out, "\"duration_ns\": %" PRIu64 "\n", elapsed);
   fprintf(utctx->out, "}\n");
}

static void
print_json_event(struct u_trace_context *utctx,
                 const struct u_trace_event *evt,
                 uint64_t ns,
                 int32_t delta,
                 const void *indirect)
{
   if (utctx->event_nr != 0)
      fprintf(utctx->out, ",\n");
   fprintf(utctx->out, "{\n\"event\": \"%s\",\n", evt->tp->name);
   fprintf(utctx->out, "\"time_ns\": \"%016" PRIu64 "\",\n", ns);
   fprintf(utctx->out, "\"params\": {");
   if (evt->tp->print)
      evt->tp->print_json(utctx->out, evt->payload, indirect);
   fprintf(utctx->out, "}\n}\n");
}

static struct u_trace_printer json_printer = {
   .start = print_json_start,
   .end = print_json_end,
   .start_of_frame = &print_json_start_of_frame,
   .end_of_frame = &print_json_end_of_frame,
   .start_of_batch = &print_json_start_of_batch,
   .end_of_batch = &print_json_end_of_batch,
   .event = &print_json_event,
};

static const struct debug_named_value config_control[] = {
   { "print", U_TRACE_TYPE_PRINT, "Enable print" },
   { "print_csv", U_TRACE_TYPE_PRINT_CSV, "Enable print in CSV" },
   { "print_json", U_TRACE_TYPE_PRINT_JSON, "Enable print in JSON" },
#ifdef HAVE_PERFETTO
   { "perfetto", U_TRACE_TYPE_PERFETTO_ENV, "Enable perfetto" },
#endif
   { "markers", U_TRACE_TYPE_MARKERS, "Enable marker trace" },
   { "indirects", U_TRACE_TYPE_INDIRECTS, "Enable indirect data capture" },
   { "ranges", U_TRACE_TYPE_RANGES, "Tracepoint ranges print" },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_OPTION(trace_file, "MESA_GPU_TRACEFILE", NULL)

static void
trace_file_fini(void)
{
   fclose(u_trace_state.trace_file);
   u_trace_state.trace_file = NULL;
}

static void
u_trace_state_init_once(void)
{
   u_trace_state.enabled_traces =
      debug_get_flags_option("MESA_GPU_TRACES", config_control, 0);
   const char *tracefile_name = debug_get_option_trace_file();
   if (tracefile_name && __normal_user()) {
      u_trace_state.trace_file = fopen(tracefile_name, "w");
      if (u_trace_state.trace_file != NULL) {
         atexit(trace_file_fini);
      }
   }
   if (!u_trace_state.trace_file) {
      u_trace_state.trace_file = stdout;
   }
}

void
u_trace_state_init(void)
{
   util_call_once(&u_trace_state.once, u_trace_state_init_once);
}

bool
u_trace_is_enabled(enum u_trace_type type)
{
   /* Active is only tracked in a given u_trace context, so if you're asking
    * us if U_TRACE_TYPE_PERFETTO (_ENV | _ACTIVE) is enabled, then just check
    * _ENV ("perfetto tracing is desired, but perfetto might not be running").
    */
   type &= ~U_TRACE_TYPE_PERFETTO_ACTIVE;

   return (u_trace_state.enabled_traces & type) == type;
}

static void
queue_init(struct u_trace_context *utctx)
{
   if (utctx->queue.jobs)
      return;

   bool ret = util_queue_init(
      &utctx->queue, "traceq", 256, 1,
      UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY | UTIL_QUEUE_INIT_RESIZE_IF_FULL,
      NULL);
   assert(ret);

   if (!ret)
      utctx->out = NULL;
}

static uint32_t
u_trace_event_fuzzy_hash(const void *_event)
{
   const struct u_trace_event *event = _event;

   uint32_t hash = _mesa_hash_pointer(event->tp);
   if (!event->tp->fuzzy_hash)
      return hash;

   return hash * event->tp->fuzzy_hash(event->payload);
}

static bool
u_trace_event_fuzzy_equals(const void *_a, const void *_b)
{
   const struct u_trace_event *a = _a;
   const struct u_trace_event *b = _b;

   if (a->tp != b->tp)
      return false;

   if (a->tp->fuzzy_equals && !a->tp->fuzzy_equals(a->payload, b->payload))
      return false;

   return true;
}

void
u_trace_context_init(struct u_trace_context *utctx,
                     void *pctx,
                     uint32_t timestamp_size_bytes,
                     uint32_t max_indirect_size_bytes,
                     u_trace_create_buffer create_buffer,
                     u_trace_delete_buffer delete_buffer,
                     u_trace_record_ts record_timestamp,
                     u_trace_read_ts read_timestamp,
                     u_trace_capture_data capture_data,
                     u_trace_get_data get_data,
                     u_trace_delete_flush_data delete_flush_data)
{
   u_trace_state_init();

   utctx->enabled_traces = u_trace_state.enabled_traces;
   utctx->pctx = pctx;
   utctx->create_buffer = create_buffer;
   utctx->delete_buffer = delete_buffer;
   utctx->record_timestamp = record_timestamp;
   utctx->capture_data = capture_data;
   utctx->get_data = get_data;
   utctx->read_timestamp = read_timestamp;
   utctx->delete_flush_data = delete_flush_data;
   utctx->timestamp_size_bytes = timestamp_size_bytes;
   utctx->max_indirect_size_bytes = max_indirect_size_bytes;

   utctx->last_time_ns = 0;
   utctx->first_time_ns = 0;
   utctx->frame_nr = 0;
   utctx->batch_nr = 0;
   utctx->event_nr = 0;
   utctx->start_of_frame = true;

   utctx->dummy_indirect_data = calloc(1, max_indirect_size_bytes);

   util_dynarray_init(&utctx->flushed_traces, NULL);

   if (utctx->enabled_traces & U_TRACE_TYPE_PRINT) {
      utctx->out = u_trace_state.trace_file;

      if (utctx->enabled_traces & U_TRACE_TYPE_JSON) {
         utctx->out_printer = &json_printer;
      } else if (utctx->enabled_traces & U_TRACE_TYPE_CSV) {
         utctx->out_printer = &csv_printer;
      } else {
         utctx->out_printer = &txt_printer;
      }
   } else {
      utctx->out = NULL;
      utctx->out_printer = NULL;
   }

   util_dynarray_init(&utctx->begin_tracepoints, NULL);
   _mesa_hash_table_init(&utctx->tracepoint_ranges, NULL, u_trace_event_fuzzy_hash, u_trace_event_fuzzy_equals);

#ifdef HAVE_PERFETTO
   simple_mtx_lock(&ctx_list_mutex);
   list_add(&utctx->node, &ctx_list);
   if (_u_trace_perfetto_count > 0)
      utctx->enabled_traces |= U_TRACE_TYPE_PERFETTO_ACTIVE;

   queue_init(utctx);

   simple_mtx_unlock(&ctx_list_mutex);
#else
   queue_init(utctx);
#endif

   if (!(p_atomic_read_relaxed(&utctx->enabled_traces) &
         U_TRACE_TYPE_REQUIRE_QUEUING))
      return;

   if (utctx->out) {
      utctx->out_printer->start(utctx);
   }
}

static void
free_tracepoint_ranges_entry(struct hash_entry *entry)
{
   struct u_trace_tracepoint_range *range = entry->data;
   _mesa_hash_table_fini(&range->child_ranges, free_tracepoint_ranges_entry);
   free(range);
   free((void *)entry->key);
}

void
u_trace_context_fini(struct u_trace_context *utctx)
{
#ifdef HAVE_PERFETTO
   simple_mtx_lock(&ctx_list_mutex);
   list_del(&utctx->node);
   simple_mtx_unlock(&ctx_list_mutex);
#endif

   util_dynarray_fini(&utctx->begin_tracepoints);
   _mesa_hash_table_fini(&utctx->tracepoint_ranges, free_tracepoint_ranges_entry);

   if (utctx->out) {
      if (utctx->batch_nr > 0) {
         utctx->out_printer->end_of_frame(utctx);
      }

      utctx->out_printer->end(utctx);
      fflush(utctx->out);
   }

   free (utctx->dummy_indirect_data);

   if (!utctx->queue.jobs)
      return;
   util_queue_finish(&utctx->queue);
   util_queue_destroy(&utctx->queue);
   util_dynarray_foreach(&utctx->flushed_traces, struct u_trace_flush *, flush) {
      u_trace_flush_destroy(*flush);
   }
   util_dynarray_fini(&utctx->flushed_traces);
}

#ifdef HAVE_PERFETTO
void
u_trace_perfetto_start(void)
{
   simple_mtx_lock(&ctx_list_mutex);

   list_for_each_entry (struct u_trace_context, utctx, &ctx_list, node) {
      queue_init(utctx);
      p_atomic_set(&utctx->enabled_traces,
                   utctx->enabled_traces | U_TRACE_TYPE_PERFETTO_ACTIVE);
   }

   _u_trace_perfetto_count++;

   simple_mtx_unlock(&ctx_list_mutex);
}

void
u_trace_perfetto_stop(void)
{
   simple_mtx_lock(&ctx_list_mutex);

   assert(_u_trace_perfetto_count > 0);
   _u_trace_perfetto_count--;
   if (_u_trace_perfetto_count == 0) {
      list_for_each_entry (struct u_trace_context, utctx, &ctx_list, node) {
         p_atomic_set(&utctx->enabled_traces,
                      utctx->enabled_traces & ~U_TRACE_TYPE_PERFETTO_ACTIVE);
      }
   }

   simple_mtx_unlock(&ctx_list_mutex);
}
#endif

enum u_trace_buffer_list_index {
   u_trace_buffer_list_timestamps,
   u_trace_buffer_list_indirect,
};

struct u_trace_buffer {
   void *buffer;
   uint32_t alloc_offset;
   uint32_t size;
};

static void *
u_trace_buffer_view_get_buffer(struct u_trace *ut, enum u_trace_buffer_list_index list_index, struct u_trace_buffer_view view)
{
   struct util_dynarray *list = &ut->buffers[list_index];
   struct u_trace_buffer *buffer = util_dynarray_element(list, struct u_trace_buffer, view.buffer_index);
   return buffer->buffer;
}

static char *
print_time(void *ctx, double time_ns)
{
   if (time_ns < 1000)
      return ralloc_asprintf(ctx, "%.2fns", time_ns);
   if (time_ns < 1000 * 1000)
      return ralloc_asprintf(ctx, "%.2fus", time_ns / 1000);
   if (time_ns < 1000 * 1000 * 1000)
      return ralloc_asprintf(ctx, "%.2fms", time_ns / (1000 * 1000));
   return ralloc_asprintf(ctx, "%.2fs", time_ns / (1000 * 1000 * 1000));
}

static int
compare_ranges(const void *_a, const void *_b)
{
   const struct hash_entry *a = _a;
   const struct hash_entry *b = _b;

   struct u_trace_tracepoint_range *range_a = a->data;
   struct u_trace_tracepoint_range *range_b = b->data;

   return range_a->duration_ns == range_b->duration_ns ? 0 : (range_a->duration_ns > range_b->duration_ns ? -1 : 1);
}

static void
print_ranges(struct u_trace_context *utctx, struct hash_table *ranges, uint32_t indentation)
{
   void *ctx = ralloc_context(NULL);

   struct hash_entry *sorted_ranges = ralloc_array(ctx, struct hash_entry, _mesa_hash_table_num_entries(ranges));
   uint32_t dst_index = 0;
   hash_table_foreach(ranges, entry) {
      sorted_ranges[dst_index] = *entry;
      dst_index++;
   }

   qsort(sorted_ranges, _mesa_hash_table_num_entries(ranges), sizeof(struct hash_entry), compare_ranges);

   for (uint32_t i = 0; i < _mesa_hash_table_num_entries(ranges); i++) {
      struct hash_entry *entry = &sorted_ranges[i];
      const struct u_trace_event *event = entry->key;
      struct u_trace_tracepoint_range *range = entry->data;
      for (uint32_t j = 0; j < indentation; j++)
         fprintf(stderr, "   ");
      fprintf(stderr, "%s ", event->tp->name);
      if (event->tp->print_fuzzy_hash_args) {
         event->tp->print_fuzzy_hash_args(stderr, event->payload);
         fprintf(stderr, " ");
      }
      fprintf(stderr, "(avg/frame=%s avg=%s count=%u total=%s)\n",
              print_time(ctx, range->duration_ns / utctx->accumulated_frame_count),
              print_time(ctx, range->duration_ns / range->count), range->count,
              print_time(ctx, range->duration_ns));
      print_ranges(utctx, &range->child_ranges, indentation + 1);
   }
   ralloc_free(ctx);
}

static void
process_flush(void *job, void *gdata, int thread_index)
{
   struct u_trace_flush *flush = job;
   struct u_trace *ut = &flush->trace;
   struct u_trace_context *utctx = ut->utctx;

   if (flush->frame_nr != U_TRACE_FRAME_UNKNOWN &&
       flush->frame_nr != utctx->frame_nr) {
      if (utctx->out) {
         utctx->out_printer->end_of_frame(utctx);
      }
      utctx->frame_nr = flush->frame_nr;
      utctx->start_of_frame = true;
   }

   if (utctx->start_of_frame) {
      utctx->start_of_frame = false;
      utctx->batch_nr = 0;
      if (utctx->out) {
         utctx->out_printer->start_of_frame(utctx);
      }
   }

   utctx->event_nr = 0;
   if (utctx->out) {
      utctx->out_printer->start_of_batch(utctx);
   }

   uint64_t last_timestamp = 0;
   struct u_trace_buffer_view last_timestamp_view = {
      .buffer_index = UINT32_MAX,
   };

   util_dynarray_foreach(&ut->events, struct u_trace_event *, _event) {
      struct u_trace_event *event = *_event;
      if (!event->tp)
         continue;

      uint64_t timestamp = last_timestamp;
      if (memcmp(&event->timestamp, &last_timestamp_view, sizeof(struct u_trace_buffer_view))) {
         void *timestamp_buffer =
            u_trace_buffer_view_get_buffer(ut, u_trace_buffer_list_timestamps, event->timestamp);
         timestamp = utctx->read_timestamp(utctx, timestamp_buffer, event->timestamp.offset,
                                           event->tp->flags, flush->flush_data);
         last_timestamp = timestamp;
         last_timestamp_view = event->timestamp;
      }

      if (utctx->enabled_traces & U_TRACE_TYPE_RANGES) {
         if (event->tp->type == u_tracepoint_type_begin_range) {
            struct u_trace_begin_tracepoint range = {
               .event = event,
               .timestamp_ns = timestamp,
            };
            util_dynarray_append(&utctx->begin_tracepoints, range);
         } else if (event->tp->type == u_tracepoint_type_end_range) {
            struct u_trace_begin_tracepoint *last_begin =
               util_dynarray_last_ptr(&utctx->begin_tracepoints, struct u_trace_begin_tracepoint);

            struct hash_table *ranges = &utctx->tracepoint_ranges;
            util_dynarray_foreach(&utctx->begin_tracepoints, struct u_trace_begin_tracepoint, begin) {
               const struct u_trace_event *begin_event = begin->event;
               struct hash_entry *entry = _mesa_hash_table_search(ranges, begin_event);
               struct u_trace_tracepoint_range *range = NULL;
               if (entry) {
                  range = entry->data;
               } else {
                  range = calloc(1, sizeof(struct u_trace_tracepoint_range));
                  _mesa_hash_table_init(&range->child_ranges, NULL, u_trace_event_fuzzy_hash, u_trace_event_fuzzy_equals);

                  uint32_t event_size = sizeof(struct u_trace_event) + begin_event->payload_size;
                  void *event_copy = malloc(event_size);
                  memcpy(event_copy, begin_event, event_size);

                  _mesa_hash_table_insert(ranges, event_copy, range);
               }
               if (begin == last_begin) {
                  range->duration_ns += timestamp - begin->timestamp_ns;
                  range->count++;
               }
               ranges = &range->child_ranges;
            }

            assert(utctx->begin_tracepoints.size);
            utctx->begin_tracepoints.size -= sizeof(struct u_trace_begin_tracepoint);
         }
      }

      int32_t delta;

      if (!utctx->first_time_ns)
         utctx->first_time_ns = timestamp;

      if (timestamp != U_TRACE_NO_TIMESTAMP) {
         delta = utctx->last_time_ns ? timestamp - utctx->last_time_ns : 0;
         utctx->last_time_ns = timestamp;
      } else {
         /* we skipped recording the timestamp, so it should be
          * the same as last msg:
          */
         timestamp = utctx->last_time_ns;
         delta = 0;
      }

      const void *indirect_data = NULL;
      if (event->indirect.buffer_index != UINT32_MAX &&
          (utctx->enabled_traces & U_TRACE_TYPE_INDIRECTS)) {
            void *indirect_buffer =
               u_trace_buffer_view_get_buffer(ut, u_trace_buffer_list_indirect, event->indirect);
            indirect_data = utctx->get_data(utctx, indirect_buffer, event->indirect.offset,
                                            event->tp->indirect_sz);
      } else if (event->tp->indirect_sz) {
         indirect_data = utctx->dummy_indirect_data;
      }

      if (utctx->out) {
         utctx->out_printer->event(utctx, event, timestamp, delta, indirect_data);
      }
#ifdef HAVE_PERFETTO
      if (event->tp->perfetto &&
          (p_atomic_read_relaxed(&utctx->enabled_traces) &
           U_TRACE_TYPE_PERFETTO_ACTIVE)) {
         event->tp->perfetto(utctx->pctx, timestamp, event->tp->tp_idx, flush->flush_data,
                             event->payload, indirect_data);
      }
#endif

      utctx->event_nr++;
   }

   if (utctx->out) {
      utctx->out_printer->end_of_batch(utctx);
   }

   utctx->batch_nr++;
   utctx->last_time_ns = 0;
   utctx->first_time_ns = 0;

   if (flush->eof) {
      if (utctx->out) {
         utctx->out_printer->end_of_frame(utctx);
      }
      utctx->frame_nr++;
      utctx->start_of_frame = true;

      if (utctx->enabled_traces & U_TRACE_TYPE_RANGES) {
         utctx->accumulated_frame_count++;
         fprintf(stderr, "TRACEPOINT RANGE STATS:\n");
         print_ranges(utctx, &utctx->tracepoint_ranges, 0);
      }
   }
}

static void
cleanup_chunk(void *job, void *gdata, int thread_index)
{
   u_trace_flush_destroy(job);
}

void
u_trace_context_process(struct u_trace_context *utctx, bool eof)
{
   if (!util_dynarray_num_elements(&utctx->flushed_traces, struct u_trace_flush *))
      return;

   struct u_trace_flush *last =
      *util_dynarray_last_ptr(&utctx->flushed_traces, struct u_trace_flush *);
   last->eof = eof;

   util_dynarray_foreach(&utctx->flushed_traces, struct u_trace_flush *, _flush) {
      struct u_trace_flush *flush = *_flush;
      util_queue_add_job(&utctx->queue, flush, &flush->fence, process_flush,
                         cleanup_chunk, TIMESTAMP_BUF_SIZE);
   }

   util_dynarray_clear(&utctx->flushed_traces);
}

static linear_ctx *
u_trace_get_linear_alloc(struct u_trace *ut)
{
   if (!ut->linear_alloc) {
      ut->linear_alloc = linear_context(NULL);
   }

   return ut->linear_alloc;
}

void
u_trace_init(struct u_trace *ut, struct u_trace_context *utctx)
{
   memset(ut, 0, sizeof(struct u_trace));
   ut->utctx = utctx;
   ut->last_timestamp.buffer_index = UINT32_MAX;
   ut->events = UTIL_DYNARRAY_INIT;
   ut->buffers[0] = UTIL_DYNARRAY_INIT;
   ut->buffers[1] = UTIL_DYNARRAY_INIT;
}

void
u_trace_move(struct u_trace *dst, struct u_trace *src)
{
   u_trace_fini(dst);
   memcpy(dst, src, sizeof(struct u_trace));
   u_trace_init(src, dst->utctx);
}

void
u_trace_fini(struct u_trace *ut)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(ut->buffers); i++) {
      util_dynarray_foreach(&ut->buffers[i], struct u_trace_buffer, buffer) {
         ut->utctx->delete_buffer(ut->utctx, buffer->buffer);
      }
      util_dynarray_fini(&ut->buffers[i]);
   }

   linear_free_context(ut->linear_alloc);
   ut->linear_alloc = NULL;
   ut->last_timestamp.buffer_index = UINT32_MAX;
   util_dynarray_fini(&ut->events);
}

bool
u_trace_has_points(struct u_trace *ut)
{
   return !!u_trace_num_events(ut);
}

uint32_t
u_trace_num_events(struct u_trace *ut)
{
   return util_dynarray_num_elements(&ut->events, struct u_trace_event *);
}

static struct u_trace_buffer_view
alloc_device_memory(struct u_trace *ut, enum u_trace_buffer_list_index list_index, uint32_t size)
{
   struct util_dynarray *list = &ut->buffers[list_index];

   size = align(size, 8);

   uint32_t buffer_count = util_dynarray_num_elements(list, struct u_trace_buffer);
   if (buffer_count) {
      struct u_trace_buffer *last = util_dynarray_last_ptr(list, struct u_trace_buffer);
      if (last->alloc_offset + size <= last->size) {
         struct u_trace_buffer_view view = {
            .buffer_index = buffer_count - 1,
            .offset = last->alloc_offset,
         };
         last->alloc_offset += size;
         return view;
      }
   }

   uint32_t min_size = TIMESTAMP_BUF_SIZE;
   if (list_index == u_trace_buffer_list_timestamps)
      min_size *= ut->utctx->timestamp_size_bytes;
   else
      min_size *= ut->utctx->max_indirect_size_bytes;

   uint32_t alloc_size = MAX2(size, min_size);

   struct u_trace_buffer new_buffer = {
      .buffer = ut->utctx->create_buffer(ut->utctx, alloc_size),
      .size = alloc_size,
   };

   util_dynarray_append(list, new_buffer);

   return alloc_device_memory(ut, list_index, size);
}

/* Release the memory that was allocated most recently back to the allocator. */
static void
release_last_device_memory(struct u_trace *ut, enum u_trace_buffer_list_index list_index, struct u_trace_buffer_view view)
{
   struct util_dynarray *list = &ut->buffers[list_index];

   ASSERTED uint32_t buffer_count = util_dynarray_num_elements(list, struct u_trace_buffer);
   assert(view.buffer_index == buffer_count - 1);

   struct u_trace_buffer *last = util_dynarray_last_ptr(list, struct u_trace_buffer);
   last->alloc_offset = view.offset;
}

void
u_trace_clone_append(struct u_trace_iterator begin_it,
                     struct u_trace_iterator end_it,
                     struct u_trace *into,
                     void *cmdstream,
                     u_trace_copy_buffer copy_buffer)
{
   assert(begin_it.ut == end_it.ut);
   struct u_trace *from = begin_it.ut;

   if (u_trace_iterator_equal(begin_it, end_it))
      return;

   linear_ctx *linear_alloc = u_trace_get_linear_alloc(into);

   struct u_trace_buffer_view src_timestamp = {0}, dst_timestamp = {0}, src_indirect = {0}, dst_indirect = {0};
   src_timestamp.buffer_index = UINT32_MAX;
   src_indirect.buffer_index = UINT32_MAX;

   for (uint32_t i = begin_it.event_idx; i < end_it.event_idx; i++) {
      struct u_trace_event *src_event = *util_dynarray_element(&from->events, struct u_trace_event *, i);

      if (src_timestamp.buffer_index != src_event->timestamp.buffer_index) {
         src_timestamp = src_event->timestamp;
         struct u_trace_buffer *src_buffer =
            util_dynarray_element(&from->buffers[u_trace_buffer_list_timestamps], struct u_trace_buffer, src_event->timestamp.buffer_index);
         dst_timestamp = alloc_device_memory(into, u_trace_buffer_list_timestamps, src_buffer->size);

         void *dst_buffer = u_trace_buffer_view_get_buffer(into, u_trace_buffer_list_timestamps, dst_timestamp);
         copy_buffer(into->utctx, cmdstream, src_buffer->buffer, 0, dst_buffer, dst_timestamp.offset, src_buffer->size);
      }

      if (src_event->indirect.buffer_index != UINT32_MAX &&
          src_indirect.buffer_index != src_event->indirect.buffer_index) {
         src_indirect = src_event->indirect;
         struct u_trace_buffer *src_buffer =
            util_dynarray_element(&from->buffers[u_trace_buffer_list_indirect], struct u_trace_buffer, src_event->indirect.buffer_index);
         dst_indirect = alloc_device_memory(into, u_trace_buffer_list_indirect, src_buffer->size);

         void *dst_buffer = u_trace_buffer_view_get_buffer(into, u_trace_buffer_list_indirect, dst_indirect);
         copy_buffer(into->utctx, cmdstream, src_buffer->buffer, 0, dst_buffer, dst_indirect.offset, src_buffer->size);
      }

      struct u_trace_event *dst_event = linear_alloc_child(linear_alloc, sizeof(struct u_trace_event) + src_event->payload_size);
      memcpy(dst_event, src_event, sizeof(struct u_trace_event) + src_event->payload_size);
      dst_event->timestamp.buffer_index = dst_timestamp.buffer_index;
      dst_event->timestamp.offset = dst_timestamp.offset + src_event->timestamp.offset;
      if (src_event->indirect.buffer_index != UINT32_MAX) {
         dst_event->indirect.buffer_index = dst_indirect.buffer_index;
         dst_event->indirect.offset = dst_indirect.offset + src_event->indirect.offset;
      }

      util_dynarray_append(&into->events, dst_event);
   }
}

uint32_t
u_trace_clone_append_copy_count(struct u_trace_iterator begin_it,
                                struct u_trace_iterator end_it)
{
   assert(begin_it.ut == end_it.ut);
   struct u_trace *from = begin_it.ut;

   struct u_trace_buffer_view src_timestamp = {0}, src_indirect = {0};
   src_timestamp.buffer_index = UINT32_MAX;
   src_indirect.buffer_index = UINT32_MAX;

   uint32_t copy_count = 0;

   for (uint32_t i = begin_it.event_idx; i < end_it.event_idx; i++) {
      struct u_trace_event *src_event = *util_dynarray_element(&from->events, struct u_trace_event *, i);

      if (src_timestamp.buffer_index != src_event->timestamp.buffer_index) {
         src_timestamp.buffer_index = src_event->timestamp.buffer_index;
         copy_count++;
      }

      if (src_event->indirect.buffer_index != UINT32_MAX &&
          src_indirect.buffer_index != src_event->indirect.buffer_index) {
         src_indirect.buffer_index = src_event->indirect.buffer_index;
         copy_count++;
      }
   }

   return copy_count;
}

void
u_trace_disable_event_range(struct u_trace_iterator begin_it,
                            struct u_trace_iterator end_it)
{
   assert(begin_it.ut == end_it.ut);
   struct u_trace *ut = begin_it.ut;

   for (uint32_t i = begin_it.event_idx; i < end_it.event_idx; i++) {
      struct u_trace_event *event = *util_dynarray_element(&ut->events, struct u_trace_event *, i);
      event->tp = NULL;
   }
}

/**
 * Append a trace event, returning pointer to buffer of tp->payload_sz
 * to be filled in with trace payload.  Called by generated tracepoint
 * functions.
 */
void *
u_trace_appendv(struct u_trace *ut,
                void *cs,
                const struct u_tracepoint *tp,
                unsigned variable_sz,
                unsigned n_indirects,
                const struct u_trace_address *addresses,
                const uint8_t *indirect_sizes_B)
{
   assert(tp->payload_sz == ALIGN_NPOT(tp->payload_sz, 8));

   unsigned payload_sz = ALIGN_NPOT(tp->payload_sz + variable_sz, 8);
   struct u_trace_event *event =
      linear_alloc_child(u_trace_get_linear_alloc(ut),
                         sizeof(struct u_trace_event) + payload_sz);
   event->tp = tp;
   event->payload_size = payload_sz;

   struct u_trace_buffer_view dst_timestamp =
      alloc_device_memory(ut, u_trace_buffer_list_timestamps, ut->utctx->timestamp_size_bytes);
   void *dst_timestamp_buffer =
      u_trace_buffer_view_get_buffer(ut, u_trace_buffer_list_timestamps, dst_timestamp);

   /* record a timestamp for the trace: */
   bool new_timestamp = ut->utctx->record_timestamp(
      ut, cs, dst_timestamp_buffer, dst_timestamp.offset, tp->flags);

   event->timestamp = dst_timestamp;
   if (new_timestamp) {
      ut->last_timestamp = dst_timestamp;
   } else {
      assert(ut->last_timestamp.buffer_index != UINT32_MAX);
      event->timestamp = ut->last_timestamp;
      release_last_device_memory(ut, u_trace_buffer_list_timestamps, dst_timestamp);
   }

   event->indirect.buffer_index = UINT32_MAX;
   if ((ut->utctx->enabled_traces & U_TRACE_TYPE_INDIRECTS) && ut->utctx->max_indirect_size_bytes && n_indirects) {
      struct u_trace_buffer_view dst_indirect =
         alloc_device_memory(ut, u_trace_buffer_list_indirect, ut->utctx->max_indirect_size_bytes);
      void *dst_indirect_buffer =
         u_trace_buffer_view_get_buffer(ut, u_trace_buffer_list_indirect, dst_indirect);

      event->indirect = dst_indirect;

      uint64_t dst_offset = 0;
      for (unsigned i = 0; i < n_indirects; i++) {
         ut->utctx->capture_data(
            ut, cs, dst_indirect_buffer, dst_indirect.offset + dst_offset,
            addresses[i].bo, addresses[i].offset, indirect_sizes_B[i]);
         dst_offset += indirect_sizes_B[i];
      }

   }

   util_dynarray_append(&ut->events, event);

   return event->payload;
}

void
u_trace_flush(struct u_trace *ut,
              void *flush_data,
              uint32_t frame_nr,
              bool free_data)
{

   struct u_trace_flush *flush = calloc(1, sizeof(struct u_trace_flush));
   flush->flush_data = flush_data;
   flush->free_flush_data = free_data;
   flush->frame_nr = frame_nr;
   u_trace_move(&flush->trace, ut);
   util_dynarray_append(&ut->utctx->flushed_traces, flush);
}
