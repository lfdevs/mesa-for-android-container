/*
 * Copyright © 2018 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_defines.h"
#include "brw_performance_query.h"

#include "perf/gen_perf.h"
#include "perf/gen_perf_mdapi.h"

static void
fill_mdapi_perf_query_counter(struct gen_perf_query_info *query,
                              const char *name,
                              uint32_t data_offset,
                              uint32_t data_size,
                              enum gen_perf_counter_data_type data_type)
{
   struct gen_perf_query_counter *counter = &query->counters[query->n_counters];

   assert(query->n_counters <= query->max_counters);

   counter->name = name;
   counter->desc = "Raw counter value";
   counter->type = GEN_PERF_COUNTER_TYPE_RAW;
   counter->data_type = data_type;
   counter->offset = data_offset;

   query->n_counters++;

   assert(counter->offset + gen_perf_query_counter_get_size(counter) <= query->data_size);
}

#define MDAPI_QUERY_ADD_COUNTER(query, struct_name, field_name, type_name) \
   fill_mdapi_perf_query_counter(query, #field_name,                    \
                                 (uint8_t *) &struct_name.field_name -  \
                                 (uint8_t *) &struct_name,              \
                                 sizeof(struct_name.field_name),        \
                                 GEN_PERF_COUNTER_DATA_TYPE_##type_name)
#define MDAPI_QUERY_ADD_ARRAY_COUNTER(ctx, query, struct_name, field_name, idx, type_name) \
   fill_mdapi_perf_query_counter(query,                                 \
                                 ralloc_asprintf(ctx, "%s%i", #field_name, idx), \
                                 (uint8_t *) &struct_name.field_name[idx] - \
                                 (uint8_t *) &struct_name,              \
                                 sizeof(struct_name.field_name[0]),     \
                                 GEN_PERF_COUNTER_DATA_TYPE_##type_name)

void
brw_perf_query_register_mdapi_oa_query(struct brw_context *brw)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   struct gen_perf *perf = brw->perfquery.perf;
   struct gen_perf_query_info *query = NULL;

   /* MDAPI requires different structures for pretty much every generation
    * (right now we have definitions for gen 7 to 11).
    */
   if (!(devinfo->gen >= 7 && devinfo->gen <= 11))
      return;

   switch (devinfo->gen) {
   case 7: {
      query = gen_perf_query_append_query_info(perf, 1 + 45 + 16 + 7);
      query->oa_format = I915_OA_FORMAT_A45_B8_C8;

      struct gen7_mdapi_metrics metric_data;
      query->data_size = sizeof(metric_data);

      MDAPI_QUERY_ADD_COUNTER(query, metric_data, TotalTime, UINT64);
      for (int i = 0; i < ARRAY_SIZE(metric_data.ACounters); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, ACounters, i, UINT64);
      }
      for (int i = 0; i < ARRAY_SIZE(metric_data.NOACounters); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, NOACounters, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SplitOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequencyChanged, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportsCount, UINT32);
      break;
   }
   case 8: {
      query = gen_perf_query_append_query_info(perf, 2 + 36 + 16 + 16);
      query->oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

      struct gen8_mdapi_metrics metric_data;
      query->data_size = sizeof(metric_data);

      MDAPI_QUERY_ADD_COUNTER(query, metric_data, TotalTime, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, GPUTicks, UINT64);
      for (int i = 0; i < ARRAY_SIZE(metric_data.OaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, OaCntr, i, UINT64);
      }
      for (int i = 0; i < ARRAY_SIZE(metric_data.NoaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, NoaCntr, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, BeginTimestamp, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved3, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, OverrunOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerUser, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerDriver, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, UnsliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SplitOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequencyChanged, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportsCount, UINT32);
      break;
   }
   case 9:
   case 10:
   case 11: {
      query = gen_perf_query_append_query_info(perf, 2 + 36 + 16 + 16 + 16 + 2);
      query->oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

      struct gen9_mdapi_metrics metric_data;
      query->data_size = sizeof(metric_data);

      MDAPI_QUERY_ADD_COUNTER(query, metric_data, TotalTime, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, GPUTicks, UINT64);
      for (int i = 0; i < ARRAY_SIZE(metric_data.OaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, OaCntr, i, UINT64);
      }
      for (int i = 0; i < ARRAY_SIZE(metric_data.NoaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, NoaCntr, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, BeginTimestamp, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved3, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, OverrunOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerUser, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerDriver, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, UnsliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SplitOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequencyChanged, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportsCount, UINT32);
      for (int i = 0; i < ARRAY_SIZE(metric_data.UserCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, UserCntr, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, UserCntrCfgId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved4, UINT32);
      break;
   }
   default:
      unreachable("Unsupported gen");
      break;
   }

   query->kind = GEN_PERF_QUERY_TYPE_RAW;
   query->name = "Intel_Raw_Hardware_Counters_Set_0_Query";
   query->guid = GEN_PERF_QUERY_GUID_MDAPI;

   {
      /* Accumulation buffer offsets copied from an actual query... */
      const struct gen_perf_query_info *copy_query =
         &brw->perfquery.perf->queries[0];

      query->gpu_time_offset = copy_query->gpu_time_offset;
      query->gpu_clock_offset = copy_query->gpu_clock_offset;
      query->a_offset = copy_query->a_offset;
      query->b_offset = copy_query->b_offset;
      query->c_offset = copy_query->c_offset;
   }
}

void
brw_perf_query_register_mdapi_statistic_query(struct brw_context *brw)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;

   if (!(devinfo->gen >= 7 && devinfo->gen <= 11))
      return;

   struct gen_perf_query_info *query =
      gen_perf_query_append_query_info(brw->perfquery.perf, MAX_STAT_COUNTERS);

   query->kind = GEN_PERF_QUERY_TYPE_PIPELINE;
   query->name = "Intel_Raw_Pipeline_Statistics_Query";

   /* The order has to match mdapi_pipeline_metrics. */
   gen_perf_query_info_add_basic_stat_reg(query, IA_VERTICES_COUNT,
                                          "N vertices submitted");
   gen_perf_query_info_add_basic_stat_reg(query, IA_PRIMITIVES_COUNT,
                                          "N primitives submitted");
   gen_perf_query_info_add_basic_stat_reg(query, VS_INVOCATION_COUNT,
                                          "N vertex shader invocations");
   gen_perf_query_info_add_basic_stat_reg(query, GS_INVOCATION_COUNT,
                                          "N geometry shader invocations");
   gen_perf_query_info_add_basic_stat_reg(query, GS_PRIMITIVES_COUNT,
                                          "N geometry shader primitives emitted");
   gen_perf_query_info_add_basic_stat_reg(query, CL_INVOCATION_COUNT,
                                          "N primitives entering clipping");
   gen_perf_query_info_add_basic_stat_reg(query, CL_PRIMITIVES_COUNT,
                                          "N primitives leaving clipping");
   if (devinfo->is_haswell || devinfo->gen == 8) {
      gen_perf_query_info_add_stat_reg(query, PS_INVOCATION_COUNT, 1, 4,
                                       "N fragment shader invocations",
                                       "N fragment shader invocations");
   } else {
      gen_perf_query_info_add_basic_stat_reg(query, PS_INVOCATION_COUNT,
                                             "N fragment shader invocations");
   }
   gen_perf_query_info_add_basic_stat_reg(query, HS_INVOCATION_COUNT,
                                          "N TCS shader invocations");
   gen_perf_query_info_add_basic_stat_reg(query, DS_INVOCATION_COUNT,
                                          "N TES shader invocations");
   if (devinfo->gen >= 7) {
      gen_perf_query_info_add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                                             "N compute shader invocations");
   }

   if (devinfo->gen >= 10) {
      /* Reuse existing CS invocation register until we can expose this new
       * one.
       */
      gen_perf_query_info_add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                                             "Reserved1");
   }

   query->data_size = sizeof(uint64_t) * query->n_counters;
}
