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

#include "gen_perf.h"
#include "gen_perf_mdapi.h"
#include "gen_perf_private.h"
#include "gen_perf_regs.h"

#include "dev/gen_device_info.h"

#include <drm-uapi/i915_drm.h>


int
gen_perf_query_result_write_mdapi(void *data, uint32_t data_size,
                                  const struct gen_device_info *devinfo,
                                  const struct gen_perf_query_result *result,
                                  uint64_t freq_start, uint64_t freq_end)
{
   switch (devinfo->gen) {
   case 7: {
      struct gen7_mdapi_metrics *mdapi_data = (struct gen7_mdapi_metrics *) data;

      if (data_size < sizeof(*mdapi_data))
         return 0;

      assert(devinfo->is_haswell);

      for (int i = 0; i < ARRAY_SIZE(mdapi_data->ACounters); i++)
         mdapi_data->ACounters[i] = result->accumulator[1 + i];

      for (int i = 0; i < ARRAY_SIZE(mdapi_data->NOACounters); i++) {
         mdapi_data->NOACounters[i] =
            result->accumulator[1 + ARRAY_SIZE(mdapi_data->ACounters) + i];
      }

      mdapi_data->ReportsCount = result->reports_accumulated;
      mdapi_data->TotalTime =
         gen_device_info_timebase_scale(devinfo, result->accumulator[0]);
      mdapi_data->CoreFrequency = freq_end;
      mdapi_data->CoreFrequencyChanged = freq_end != freq_start;
      mdapi_data->SplitOccured = result->query_disjoint;
      return sizeof(*mdapi_data);
   }
   case 8: {
      struct gen8_mdapi_metrics *mdapi_data = (struct gen8_mdapi_metrics *) data;

      if (data_size < sizeof(*mdapi_data))
         return 0;

      for (int i = 0; i < ARRAY_SIZE(mdapi_data->OaCntr); i++)
         mdapi_data->OaCntr[i] = result->accumulator[2 + i];
      for (int i = 0; i < ARRAY_SIZE(mdapi_data->NoaCntr); i++) {
         mdapi_data->NoaCntr[i] =
            result->accumulator[2 + ARRAY_SIZE(mdapi_data->OaCntr) + i];
      }

      mdapi_data->ReportId = result->hw_id;
      mdapi_data->ReportsCount = result->reports_accumulated;
      mdapi_data->TotalTime =
         gen_device_info_timebase_scale(devinfo, result->accumulator[0]);
      mdapi_data->BeginTimestamp =
         gen_device_info_timebase_scale(devinfo, result->begin_timestamp);
      mdapi_data->GPUTicks = result->accumulator[1];
      mdapi_data->CoreFrequency = freq_end;
      mdapi_data->CoreFrequencyChanged = freq_end != freq_start;
      mdapi_data->SliceFrequency =
         (result->slice_frequency[0] + result->slice_frequency[1]) / 2ULL;
      mdapi_data->UnsliceFrequency =
         (result->unslice_frequency[0] + result->unslice_frequency[1]) / 2ULL;
      mdapi_data->SplitOccured = result->query_disjoint;
      return sizeof(*mdapi_data);
   }
   case 9:
   case 11:
   case 12:{
      struct gen9_mdapi_metrics *mdapi_data = (struct gen9_mdapi_metrics *) data;

      if (data_size < sizeof(*mdapi_data))
         return 0;

      for (int i = 0; i < ARRAY_SIZE(mdapi_data->OaCntr); i++)
         mdapi_data->OaCntr[i] = result->accumulator[2 + i];
      for (int i = 0; i < ARRAY_SIZE(mdapi_data->NoaCntr); i++) {
         mdapi_data->NoaCntr[i] =
            result->accumulator[2 + ARRAY_SIZE(mdapi_data->OaCntr) + i];
      }

      mdapi_data->ReportId = result->hw_id;
      mdapi_data->ReportsCount = result->reports_accumulated;
      mdapi_data->TotalTime =
         gen_device_info_timebase_scale(devinfo, result->accumulator[0]);
      mdapi_data->BeginTimestamp =
         gen_device_info_timebase_scale(devinfo, result->begin_timestamp);
      mdapi_data->GPUTicks = result->accumulator[1];
      mdapi_data->CoreFrequency = freq_end;
      mdapi_data->CoreFrequencyChanged = freq_end != freq_start;
      mdapi_data->SliceFrequency =
         (result->slice_frequency[0] + result->slice_frequency[1]) / 2ULL;
      mdapi_data->UnsliceFrequency =
         (result->unslice_frequency[0] + result->unslice_frequency[1]) / 2ULL;
      mdapi_data->SplitOccured = result->query_disjoint;
      return sizeof(*mdapi_data);
   }
   default:
      unreachable("unexpected gen");
   }
}

void
gen_perf_register_mdapi_statistic_query(struct gen_perf_config *perf_cfg,
                                        const struct gen_device_info *devinfo)
{
   if (!(devinfo->gen >= 7 && devinfo->gen <= 12))
      return;

   struct gen_perf_query_info *query =
      gen_perf_append_query_info(perf_cfg, MAX_STAT_COUNTERS);

   query->kind = GEN_PERF_QUERY_TYPE_PIPELINE;
   query->name = "Intel_Raw_Pipeline_Statistics_Query";

   /* The order has to match mdapi_pipeline_metrics. */
   gen_perf_query_add_basic_stat_reg(query, IA_VERTICES_COUNT,
                                     "N vertices submitted");
   gen_perf_query_add_basic_stat_reg(query, IA_PRIMITIVES_COUNT,
                                     "N primitives submitted");
   gen_perf_query_add_basic_stat_reg(query, VS_INVOCATION_COUNT,
                                     "N vertex shader invocations");
   gen_perf_query_add_basic_stat_reg(query, GS_INVOCATION_COUNT,
                                     "N geometry shader invocations");
   gen_perf_query_add_basic_stat_reg(query, GS_PRIMITIVES_COUNT,
                                     "N geometry shader primitives emitted");
   gen_perf_query_add_basic_stat_reg(query, CL_INVOCATION_COUNT,
                                     "N primitives entering clipping");
   gen_perf_query_add_basic_stat_reg(query, CL_PRIMITIVES_COUNT,
                                     "N primitives leaving clipping");
   if (devinfo->is_haswell || devinfo->gen == 8) {
      gen_perf_query_add_stat_reg(query, PS_INVOCATION_COUNT, 1, 4,
                                  "N fragment shader invocations",
                                  "N fragment shader invocations");
   } else {
      gen_perf_query_add_basic_stat_reg(query, PS_INVOCATION_COUNT,
                                        "N fragment shader invocations");
   }
   gen_perf_query_add_basic_stat_reg(query, HS_INVOCATION_COUNT,
                                     "N TCS shader invocations");
   gen_perf_query_add_basic_stat_reg(query, DS_INVOCATION_COUNT,
                                     "N TES shader invocations");
   if (devinfo->gen >= 7) {
      gen_perf_query_add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                                        "N compute shader invocations");
   }

   if (devinfo->gen >= 10) {
      /* Reuse existing CS invocation register until we can expose this new
       * one.
       */
      gen_perf_query_add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                                        "Reserved1");
   }

   query->data_size = sizeof(uint64_t) * query->n_counters;
}

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
gen_perf_register_mdapi_oa_query(struct gen_perf_config *perf,
                                 const struct gen_device_info *devinfo)
{
   struct gen_perf_query_info *query = NULL;

   /* MDAPI requires different structures for pretty much every generation
    * (right now we have definitions for gen 7 to 12).
    */
   if (!(devinfo->gen >= 7 && devinfo->gen <= 12))
      return;

   switch (devinfo->gen) {
   case 7: {
      query = gen_perf_append_query_info(perf, 1 + 45 + 16 + 7);
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
      query = gen_perf_append_query_info(perf, 2 + 36 + 16 + 16);
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
   case 11:
   case 12: {
      query = gen_perf_append_query_info(perf, 2 + 36 + 16 + 16 + 16 + 2);
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
         &perf->queries[0];

      query->gpu_time_offset = copy_query->gpu_time_offset;
      query->gpu_clock_offset = copy_query->gpu_clock_offset;
      query->a_offset = copy_query->a_offset;
      query->b_offset = copy_query->b_offset;
      query->c_offset = copy_query->c_offset;
   }
}
