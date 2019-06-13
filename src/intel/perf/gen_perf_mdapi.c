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

#include "dev/gen_device_info.h"

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
      mdapi_data->GPUTicks = result->accumulator[1];
      mdapi_data->CoreFrequency = freq_end;
      mdapi_data->CoreFrequencyChanged = freq_end != freq_start;
      mdapi_data->SliceFrequency =
         (result->slice_frequency[0] + result->slice_frequency[1]) / 2ULL;
      mdapi_data->UnsliceFrequency =
         (result->unslice_frequency[0] + result->unslice_frequency[1]) / 2ULL;
      return sizeof(*mdapi_data);
   }
   case 9:
   case 10:
   case 11: {
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
      mdapi_data->GPUTicks = result->accumulator[1];
      mdapi_data->CoreFrequency = freq_end;
      mdapi_data->CoreFrequencyChanged = freq_end != freq_start;
      mdapi_data->SliceFrequency =
         (result->slice_frequency[0] + result->slice_frequency[1]) / 2ULL;
      mdapi_data->UnsliceFrequency =
         (result->unslice_frequency[0] + result->unslice_frequency[1]) / 2ULL;
      return sizeof(*mdapi_data);
   }
   default:
      unreachable("unexpected gen");
   }
}
