/*
 * Copyright © 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "iris_monitor.h"

#include <xf86drm.h>

#include "iris_screen.h"
#include "iris_context.h"

#include "perf/gen_perf.h"
#include "perf/gen_perf_regs.h"

struct iris_monitor_object {
   int num_active_counters;
   int *active_counters;

   size_t result_size;
   unsigned char *result_buffer;

   struct gen_perf_query_object *query;
};

int
iris_get_monitor_info(struct pipe_screen *pscreen, unsigned index,
                      struct pipe_driver_query_info *info)
{
   const struct iris_screen *screen = (struct iris_screen *)pscreen;
   assert(screen->monitor_cfg);
   if (!screen->monitor_cfg)
      return 0;

   const struct iris_monitor_config *monitor_cfg = screen->monitor_cfg;

   if (!info) {
      /* return the number of metrics */
      return monitor_cfg->num_counters;
   }

   const struct gen_perf_config *perf_cfg = monitor_cfg->perf_cfg;
   const int group = monitor_cfg->counters[index].group;
   const int counter_index = monitor_cfg->counters[index].counter;
   struct gen_perf_query_counter *counter =
      &perf_cfg->queries[group].counters[counter_index];

   info->group_id = group;
   info->name = counter->name;
   info->query_type = PIPE_QUERY_DRIVER_SPECIFIC + index;

   if (counter->type == GEN_PERF_COUNTER_TYPE_THROUGHPUT)
      info->result_type = PIPE_DRIVER_QUERY_RESULT_TYPE_AVERAGE;
   else
      info->result_type = PIPE_DRIVER_QUERY_RESULT_TYPE_CUMULATIVE;
   switch (counter->data_type) {
   case GEN_PERF_COUNTER_DATA_TYPE_BOOL32:
   case GEN_PERF_COUNTER_DATA_TYPE_UINT32:
      info->type = PIPE_DRIVER_QUERY_TYPE_UINT;
      info->max_value.u32 = 0;
      break;
   case GEN_PERF_COUNTER_DATA_TYPE_UINT64:
      info->type = PIPE_DRIVER_QUERY_TYPE_UINT64;
      info->max_value.u64 = 0;
      break;
   case GEN_PERF_COUNTER_DATA_TYPE_FLOAT:
   case GEN_PERF_COUNTER_DATA_TYPE_DOUBLE:
      info->type = PIPE_DRIVER_QUERY_TYPE_FLOAT;
      info->max_value.u64 = -1;
      break;
   default:
      assert(false);
      break;
   }

   /* indicates that this is an OA query, not a pipeline statistics query */
   info->flags = PIPE_DRIVER_QUERY_FLAG_BATCH;
   return 1;
}

typedef void (*bo_unreference_t)(void *);
typedef void *(*bo_map_t)(void *, void *, unsigned flags);
typedef void (*bo_unmap_t)(void *);
typedef void (*emit_mi_report_t)(void *, void *, uint32_t, uint32_t);
typedef void (*emit_mi_flush_t)(void *);
typedef void (*capture_frequency_stat_register_t)(void *, void *,
                                                  uint32_t );
typedef void (*store_register_mem64_t)(void *ctx, void *bo,
                                       uint32_t reg, uint32_t offset);
typedef bool (*batch_references_t)(void *batch, void *bo);
typedef void (*bo_wait_rendering_t)(void *bo);
typedef int (*bo_busy_t)(void *bo);

static void *
iris_oa_bo_alloc(void *bufmgr, const char *name, uint64_t size)
{
   return iris_bo_alloc(bufmgr, name, size, IRIS_MEMZONE_OTHER);
}

static void
iris_monitor_emit_mi_flush(struct iris_context *ice)
{
   const int flags = PIPE_CONTROL_RENDER_TARGET_FLUSH |
                     PIPE_CONTROL_INSTRUCTION_INVALIDATE |
                     PIPE_CONTROL_CONST_CACHE_INVALIDATE |
                     PIPE_CONTROL_DATA_CACHE_FLUSH |
                     PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                     PIPE_CONTROL_VF_CACHE_INVALIDATE |
                     PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
                     PIPE_CONTROL_CS_STALL;
   iris_emit_pipe_control_flush(&ice->batches[IRIS_BATCH_RENDER],
                                "OA metrics", flags);
}

static void
iris_monitor_emit_mi_report_perf_count(void *c,
                                       void *bo,
                                       uint32_t offset_in_bytes,
                                       uint32_t report_id)
{
   struct iris_context *ice = c;
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];
   ice->vtbl.emit_mi_report_perf_count(batch, bo, offset_in_bytes, report_id);
}

static void
iris_monitor_batchbuffer_flush(void *c, const char *file, int line)
{
   struct iris_context *ice = c;
   _iris_batch_flush(&ice->batches[IRIS_BATCH_RENDER], __FILE__, __LINE__);
}

static void
iris_monitor_capture_frequency_stat_register(void *ctx,
                                             void *bo,
                                             uint32_t bo_offset)
{
   struct iris_context *ice = ctx;
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];
   ice->vtbl.store_register_mem32(batch, GEN9_RPSTAT0, bo, bo_offset, false);
}

static void
iris_monitor_store_register_mem64(void *ctx, void *bo,
                                  uint32_t reg, uint32_t offset)
{
   struct iris_context *ice = ctx;
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];
   ice->vtbl.store_register_mem64(batch, reg, bo, offset, false);
}


static bool
iris_monitor_init_metrics(struct iris_screen *screen)
{
   struct iris_monitor_config *monitor_cfg =
      rzalloc(screen, struct iris_monitor_config);
   struct gen_perf_config *perf_cfg = NULL;
   if (unlikely(!monitor_cfg))
      goto allocation_error;
   perf_cfg = gen_perf_new(monitor_cfg);
   if (unlikely(!perf_cfg))
      goto allocation_error;

   monitor_cfg->perf_cfg = perf_cfg;

   perf_cfg->vtbl.bo_alloc = iris_oa_bo_alloc;
   perf_cfg->vtbl.bo_unreference = (bo_unreference_t)iris_bo_unreference;
   perf_cfg->vtbl.bo_map = (bo_map_t)iris_bo_map;
   perf_cfg->vtbl.bo_unmap = (bo_unmap_t)iris_bo_unmap;
   perf_cfg->vtbl.emit_mi_flush = (emit_mi_flush_t)iris_monitor_emit_mi_flush;

   perf_cfg->vtbl.emit_mi_report_perf_count =
      (emit_mi_report_t)iris_monitor_emit_mi_report_perf_count;
   perf_cfg->vtbl.batchbuffer_flush = iris_monitor_batchbuffer_flush;
   perf_cfg->vtbl.capture_frequency_stat_register =
      (capture_frequency_stat_register_t) iris_monitor_capture_frequency_stat_register;
   perf_cfg->vtbl.store_register_mem64 =
      (store_register_mem64_t) iris_monitor_store_register_mem64;
   perf_cfg->vtbl.batch_references = (batch_references_t)iris_batch_references;
   perf_cfg->vtbl.bo_wait_rendering =
      (bo_wait_rendering_t)iris_bo_wait_rendering;
   perf_cfg->vtbl.bo_busy = (bo_busy_t)iris_bo_busy;

   gen_perf_init_metrics(perf_cfg, &screen->devinfo, screen->fd);
   screen->monitor_cfg = monitor_cfg;

   /* a gallium "group" is equivalent to a gen "query"
    * a gallium "query" is equivalent to a gen "query_counter"
    *
    * Each gen_query supports a specific number of query_counters.  To
    * allocate the array of iris_monitor_counter, we need an upper bound
    * (ignoring duplicate query_counters).
    */
   int gen_query_counters_count = 0;
   for (int gen_query_id = 0;
        gen_query_id < perf_cfg->n_queries;
        ++gen_query_id) {
      gen_query_counters_count += perf_cfg->queries[gen_query_id].n_counters;
   }

   monitor_cfg->counters = rzalloc_size(monitor_cfg,
                                        sizeof(struct iris_monitor_counter) *
                                        gen_query_counters_count);
   if (unlikely(!monitor_cfg->counters))
      goto allocation_error;

   int iris_monitor_id = 0;
   for (int group = 0; group < perf_cfg->n_queries; ++group) {
      for (int counter = 0;
           counter < perf_cfg->queries[group].n_counters;
           ++counter) {
         /* Check previously identified metrics to filter out duplicates. The
          * user is not helped by having the same metric available in several
          * groups. (n^2 algorithm).
          */
         bool duplicate = false;
         for (int existing_group = 0;
              existing_group < group && !duplicate;
              ++existing_group) {
            for (int existing_counter = 0;
                 existing_counter < perf_cfg->queries[existing_group].n_counters && !duplicate;
                 ++existing_counter) {
               const char *current_name =
                  perf_cfg->queries[group].counters[counter].name;
               const char *existing_name =
                  perf_cfg->queries[existing_group].counters[existing_counter].name;
               if (strcmp(current_name, existing_name) == 0) {
                  duplicate = true;
               }
            }
         }
         if (duplicate)
            continue;
         monitor_cfg->counters[iris_monitor_id].group = group;
         monitor_cfg->counters[iris_monitor_id].counter = counter;
         ++iris_monitor_id;
      }
   }
   monitor_cfg->num_counters = iris_monitor_id;
   return monitor_cfg->num_counters;

allocation_error:
   if (monitor_cfg)
      free(monitor_cfg->counters);
   free(perf_cfg);
   free(monitor_cfg);
   return false;
}

int
iris_get_monitor_group_info(struct pipe_screen *pscreen,
                            unsigned group_index,
                            struct pipe_driver_query_group_info *info)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   if (!screen->monitor_cfg) {
      if (!iris_monitor_init_metrics(screen))
         return 0;
   }

   const struct iris_monitor_config *monitor_cfg = screen->monitor_cfg;
   const struct gen_perf_config *perf_cfg = monitor_cfg->perf_cfg;

   if (!info) {
      /* return the count that can be queried */
      return perf_cfg->n_queries;
   }

   if (group_index >= perf_cfg->n_queries) {
      /* out of range */
      return 0;
   }

   struct gen_perf_query_info *query = &perf_cfg->queries[group_index];

   info->name = query->name;
   info->max_active_queries = query->n_counters;
   info->num_queries = query->n_counters;

   return 1;
}

static void
iris_init_monitor_ctx(struct iris_context *ice)
{
   struct iris_screen *screen = (struct iris_screen *) ice->ctx.screen;
   struct iris_monitor_config *monitor_cfg = screen->monitor_cfg;

   ice->perf_ctx = gen_perf_new_context(ice);
   if (unlikely(!ice->perf_ctx))
      return;

   struct gen_perf_context *perf_ctx = ice->perf_ctx;
   struct gen_perf_config *perf_cfg = monitor_cfg->perf_cfg;
   gen_perf_init_context(perf_ctx,
                         perf_cfg,
                         ice,
                         screen->bufmgr,
                         &screen->devinfo,
                         ice->batches[IRIS_BATCH_RENDER].hw_ctx_id,
                         screen->fd);
}

/* entry point for GenPerfMonitorsAMD */
struct iris_monitor_object *
iris_create_monitor_object(struct iris_context *ice,
                           unsigned num_queries,
                           unsigned *query_types)
{
   struct iris_screen *screen = (struct iris_screen *) ice->ctx.screen;
   struct iris_monitor_config *monitor_cfg = screen->monitor_cfg;
   struct gen_perf_config *perf_cfg = monitor_cfg->perf_cfg;
   struct gen_perf_query_object *query_obj = NULL;

   /* initialize perf context if this has not already been done.  This
    * function is the first entry point that carries the gl context.
    */
   if (ice->perf_ctx == NULL) {
      iris_init_monitor_ctx(ice);
   }
   struct gen_perf_context *perf_ctx = ice->perf_ctx;

   assert(num_queries > 0);
   int query_index = query_types[0] - PIPE_QUERY_DRIVER_SPECIFIC;
   assert(query_index <= monitor_cfg->num_counters);
   const int group = monitor_cfg->counters[query_index].group;

   struct iris_monitor_object *monitor =
      calloc(1, sizeof(struct iris_monitor_object));
   if (unlikely(!monitor))
      goto allocation_failure;

   monitor->num_active_counters = num_queries;
   monitor->active_counters = calloc(num_queries, sizeof(int));
   if (unlikely(!monitor->active_counters))
      goto allocation_failure;

   for (int i = 0; i < num_queries; ++i) {
      unsigned current_query = query_types[i];
      unsigned current_query_index = current_query - PIPE_QUERY_DRIVER_SPECIFIC;

      /* all queries must be in the same group */
      assert(current_query_index <= monitor_cfg->num_counters);
      assert(monitor_cfg->counters[current_query_index].group == group);
      monitor->active_counters[i] =
         monitor_cfg->counters[current_query_index].counter;
   }

   /* create the gen_perf_query */
   query_obj = gen_perf_new_query(perf_ctx, group);
   if (unlikely(!query_obj))
      goto allocation_failure;

   monitor->query = query_obj;
   monitor->result_size = perf_cfg->queries[group].data_size;
   monitor->result_buffer = calloc(1, monitor->result_size);
   if (unlikely(!monitor->result_buffer))
      goto allocation_failure;

   return monitor;

allocation_failure:
   if (monitor) {
      free(monitor->active_counters);
      free(monitor->result_buffer);
   }
   free(query_obj);
   free(monitor);
   return NULL;
}

void
iris_destroy_monitor_object(struct pipe_context *ctx,
                            struct iris_monitor_object *monitor)
{
   struct iris_context *ice = (struct iris_context *)ctx;

   gen_perf_delete_query(ice->perf_ctx, monitor->query);
   free(monitor->result_buffer);
   monitor->result_buffer = NULL;
   free(monitor->active_counters);
   monitor->active_counters = NULL;
   free(monitor);
}

bool
iris_begin_monitor(struct pipe_context *ctx,
                   struct iris_monitor_object *monitor)
{
   struct iris_context *ice = (void *) ctx;
   struct gen_perf_context *perf_ctx = ice->perf_ctx;

   return gen_perf_begin_query(perf_ctx, monitor->query);
}

bool
iris_end_monitor(struct pipe_context *ctx,
                 struct iris_monitor_object *monitor)
{
   struct iris_context *ice = (void *) ctx;
   struct gen_perf_context *perf_ctx = ice->perf_ctx;

   gen_perf_end_query(perf_ctx, monitor->query);
   return true;
}

bool
iris_get_monitor_result(struct pipe_context *ctx,
                        struct iris_monitor_object *monitor,
                        bool wait,
                        union pipe_numeric_type_union *result)
{
   struct iris_context *ice = (void *) ctx;
   struct gen_perf_context *perf_ctx = ice->perf_ctx;
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];

   bool monitor_ready =
      gen_perf_is_query_ready(perf_ctx, monitor->query, batch);

   if (!monitor_ready) {
      if (!wait)
         return false;
      gen_perf_wait_query(perf_ctx, monitor->query, batch);
   }

   assert(gen_perf_is_query_ready(perf_ctx, monitor->query, batch));

   unsigned bytes_written;
   gen_perf_get_query_data(perf_ctx, monitor->query,
                           monitor->result_size,
                           (unsigned*) monitor->result_buffer,
                           &bytes_written);
   if (bytes_written != monitor->result_size)
      return false;

   /* copy metrics into the batch result */
   for (int i = 0; i < monitor->num_active_counters; ++i) {
      int current_counter = monitor->active_counters[i];
      const struct gen_perf_query_info *info =
         gen_perf_query_info(monitor->query);
      const struct gen_perf_query_counter *counter =
         &info->counters[current_counter];
      assert(gen_perf_query_counter_get_size(counter));
      switch (counter->data_type) {
      case GEN_PERF_COUNTER_DATA_TYPE_UINT64:
         result[i].u64 = *(uint64_t*)(monitor->result_buffer + counter->offset);
         break;
      case GEN_PERF_COUNTER_DATA_TYPE_FLOAT:
         result[i].f = *(float*)(monitor->result_buffer + counter->offset);
         break;
      case GEN_PERF_COUNTER_DATA_TYPE_UINT32:
      case GEN_PERF_COUNTER_DATA_TYPE_BOOL32:
         result[i].u64 = *(uint32_t*)(monitor->result_buffer + counter->offset);
         break;
      case GEN_PERF_COUNTER_DATA_TYPE_DOUBLE: {
         double v = *(double*)(monitor->result_buffer + counter->offset);
         result[i].f = v;
         break;
      }
      default:
         unreachable("unexpected counter data type");
      }
   }
   return true;
}
