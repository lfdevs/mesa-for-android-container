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

#ifndef GEN_PERF_MDAPI_H
#define GEN_PERF_MDAPI_H

#include <stdint.h>

#include "dev/gen_device_info.h"

struct gen_perf_query_result;

/* Guid has to matches with MDAPI's. */
#define GEN_PERF_QUERY_GUID_MDAPI "2f01b241-7014-42a7-9eb6-a925cad3daba"

/*
 * Data format expected by MDAPI.
 */

struct gen7_mdapi_metrics {
   uint64_t TotalTime;

   uint64_t ACounters[45];
   uint64_t NOACounters[16];

   uint64_t PerfCounter1;
   uint64_t PerfCounter2;
   uint32_t SplitOccured;
   uint32_t CoreFrequencyChanged;
   uint64_t CoreFrequency;
   uint32_t ReportId;
   uint32_t ReportsCount;
};

#define GTDI_QUERY_BDW_METRICS_OA_COUNT         36
#define GTDI_QUERY_BDW_METRICS_OA_40b_COUNT     32
#define GTDI_QUERY_BDW_METRICS_NOA_COUNT        16
struct gen8_mdapi_metrics {
   uint64_t TotalTime;
   uint64_t GPUTicks;
   uint64_t OaCntr[GTDI_QUERY_BDW_METRICS_OA_COUNT];
   uint64_t NoaCntr[GTDI_QUERY_BDW_METRICS_NOA_COUNT];
   uint64_t BeginTimestamp;
   uint64_t Reserved1;
   uint64_t Reserved2;
   uint32_t Reserved3;
   uint32_t OverrunOccured;
   uint64_t MarkerUser;
   uint64_t MarkerDriver;

   uint64_t SliceFrequency;
   uint64_t UnsliceFrequency;
   uint64_t PerfCounter1;
   uint64_t PerfCounter2;
   uint32_t SplitOccured;
   uint32_t CoreFrequencyChanged;
   uint64_t CoreFrequency;
   uint32_t ReportId;
   uint32_t ReportsCount;
};

#define GTDI_MAX_READ_REGS 16

struct gen9_mdapi_metrics {
   uint64_t TotalTime;
   uint64_t GPUTicks;
   uint64_t OaCntr[GTDI_QUERY_BDW_METRICS_OA_COUNT];
   uint64_t NoaCntr[GTDI_QUERY_BDW_METRICS_NOA_COUNT];
   uint64_t BeginTimestamp;
   uint64_t Reserved1;
   uint64_t Reserved2;
   uint32_t Reserved3;
   uint32_t OverrunOccured;
   uint64_t MarkerUser;
   uint64_t MarkerDriver;

   uint64_t SliceFrequency;
   uint64_t UnsliceFrequency;
   uint64_t PerfCounter1;
   uint64_t PerfCounter2;
   uint32_t SplitOccured;
   uint32_t CoreFrequencyChanged;
   uint64_t CoreFrequency;
   uint32_t ReportId;
   uint32_t ReportsCount;

   uint64_t UserCntr[GTDI_MAX_READ_REGS];
   uint32_t UserCntrCfgId;
   uint32_t Reserved4;
};

/* Add new definition */
#define gen11_mdapi_metrics gen9_mdapi_metrics

struct mdapi_pipeline_metrics {
   uint64_t IAVertices;
   uint64_t IAPrimitives;
   uint64_t VSInvocations;
   uint64_t GSInvocations;
   uint64_t GSPrimitives;
   uint64_t CInvocations;
   uint64_t CPrimitives;
   uint64_t PSInvocations;
   uint64_t HSInvocations;
   uint64_t DSInvocations;
   uint64_t CSInvocations;
   uint64_t Reserved1; /* Gen10+ */
};

int gen_perf_query_result_write_mdapi(void *data, uint32_t data_size,
                                      const struct gen_device_info *devinfo,
                                      const struct gen_perf_query_result *result,
                                      uint64_t freq_start, uint64_t freq_end);

static inline void gen_perf_query_mdapi_write_perfcntr(void *data, uint32_t data_size,
                                                       const struct gen_device_info *devinfo,
                                                       const uint64_t *begin_perf_cntrs,
                                                       const uint64_t *end_perf_cntrs)
{
   /* Only bits 0:43 of the 64bit registers contains the value. */
   const uint64_t mask = (1ull << 44) - 1;

   switch (devinfo->gen) {
   case 8: {
      if (data_size < sizeof(struct gen8_mdapi_metrics))
         return;
      struct gen8_mdapi_metrics *mdapi_data = data;
      mdapi_data->PerfCounter1 =
         (end_perf_cntrs[0] & mask) - (begin_perf_cntrs[0] & mask);
      mdapi_data->PerfCounter2 =
         (end_perf_cntrs[1] & mask) - (begin_perf_cntrs[1] & mask);
      break;
   }
   case 9:
   case 11: {
      if (data_size < sizeof(struct gen9_mdapi_metrics))
         return;
      struct gen9_mdapi_metrics *mdapi_data = data;
      mdapi_data->PerfCounter1 =
         (end_perf_cntrs[0] & mask) - (begin_perf_cntrs[0] & mask);
      mdapi_data->PerfCounter2 =
         (end_perf_cntrs[1] & mask) - (begin_perf_cntrs[1] & mask);
      break;
   }
   default:
      break;
   }
}

static inline void gen_perf_query_mdapi_write_marker(void *data, uint32_t data_size,
                                                     const struct gen_device_info *devinfo,
                                                     uint64_t value)
{
   switch (devinfo->gen) {
   case 8: {
      if (data_size < sizeof(struct gen8_mdapi_metrics))
         return;
      struct gen8_mdapi_metrics *mdapi_data = data;
      mdapi_data->MarkerUser = value;
      break;
   }
   case 9:
   case 11: {
      if (data_size < sizeof(struct gen9_mdapi_metrics))
         return;
      struct gen9_mdapi_metrics *mdapi_data = data;
      mdapi_data->MarkerUser = value;
      break;
   }
   default:
      break;
   }
}

#endif /* GEN_PERF_MDAPI_H */
