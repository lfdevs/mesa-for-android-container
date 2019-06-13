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

#ifndef GEN_PERF_H
#define GEN_PERF_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sys/sysmacros.h>

#include "util/hash_table.h"
#include "util/ralloc.h"

struct gen_device_info;

struct gen_perf;
struct gen_perf_query_info;

enum gen_perf_counter_type {
   GEN_PERF_COUNTER_TYPE_EVENT,
   GEN_PERF_COUNTER_TYPE_DURATION_NORM,
   GEN_PERF_COUNTER_TYPE_DURATION_RAW,
   GEN_PERF_COUNTER_TYPE_THROUGHPUT,
   GEN_PERF_COUNTER_TYPE_RAW,
   GEN_PERF_COUNTER_TYPE_TIMESTAMP,
};

enum gen_perf_counter_data_type {
   GEN_PERF_COUNTER_DATA_TYPE_BOOL32,
   GEN_PERF_COUNTER_DATA_TYPE_UINT32,
   GEN_PERF_COUNTER_DATA_TYPE_UINT64,
   GEN_PERF_COUNTER_DATA_TYPE_FLOAT,
   GEN_PERF_COUNTER_DATA_TYPE_DOUBLE,
};

struct gen_pipeline_stat {
   uint32_t reg;
   uint32_t numerator;
   uint32_t denominator;
};

/*
 * The largest OA formats we can use include:
 * For Haswell:
 *   1 timestamp, 45 A counters, 8 B counters and 8 C counters.
 * For Gen8+
 *   1 timestamp, 1 clock, 36 A counters, 8 B counters and 8 C counters
 */
#define MAX_OA_REPORT_COUNTERS 62

struct gen_perf_query_result {
   /**
    * Storage for the final accumulated OA counters.
    */
   uint64_t accumulator[MAX_OA_REPORT_COUNTERS];

   /**
    * Hw ID used by the context on which the query was running.
    */
   uint32_t hw_id;

   /**
    * Number of reports accumulated to produce the results.
    */
   uint32_t reports_accumulated;

   /**
    * Frequency in the slices of the GT at the begin and end of the
    * query.
    */
   uint64_t slice_frequency[2];

   /**
    * Frequency in the unslice of the GT at the begin and end of the
    * query.
    */
   uint64_t unslice_frequency[2];
};

struct gen_perf_query_counter {
   const char *name;
   const char *desc;
   enum gen_perf_counter_type type;
   enum gen_perf_counter_data_type data_type;
   uint64_t raw_max;
   size_t offset;

   union {
      uint64_t (*oa_counter_read_uint64)(struct gen_perf *perf,
                                         const struct gen_perf_query_info *query,
                                         const uint64_t *accumulator);
      float (*oa_counter_read_float)(struct gen_perf *perf,
                                     const struct gen_perf_query_info *query,
                                     const uint64_t *accumulator);
      struct gen_pipeline_stat pipeline_stat;
   };
};

struct gen_perf_query_register_prog {
   uint32_t reg;
   uint32_t val;
};

struct gen_perf_query_info {
   enum gen_perf_query_type {
      GEN_PERF_QUERY_TYPE_OA,
      GEN_PERF_QUERY_TYPE_RAW,
      GEN_PERF_QUERY_TYPE_PIPELINE,
   } kind;
   const char *name;
   const char *guid;
   struct gen_perf_query_counter *counters;
   int n_counters;
   int max_counters;
   size_t data_size;

   /* OA specific */
   uint64_t oa_metrics_set_id;
   int oa_format;

   /* For indexing into the accumulator[] ... */
   int gpu_time_offset;
   int gpu_clock_offset;
   int a_offset;
   int b_offset;
   int c_offset;

   /* Register programming for a given query */
   struct gen_perf_query_register_prog *flex_regs;
   uint32_t n_flex_regs;

   struct gen_perf_query_register_prog *mux_regs;
   uint32_t n_mux_regs;

   struct gen_perf_query_register_prog *b_counter_regs;
   uint32_t n_b_counter_regs;
};

struct gen_perf {
   struct gen_perf_query_info *queries;
   int n_queries;

   /* Variables referenced in the XML meta data for OA performance
    * counters, e.g in the normalization equations.
    *
    * All uint64_t for consistent operand types in generated code
    */
   struct {
      uint64_t timestamp_frequency; /** $GpuTimestampFrequency */
      uint64_t n_eus;               /** $EuCoresTotalCount */
      uint64_t n_eu_slices;         /** $EuSlicesTotalCount */
      uint64_t n_eu_sub_slices;     /** $EuSubslicesTotalCount */
      uint64_t eu_threads_count;    /** $EuThreadsCount */
      uint64_t slice_mask;          /** $SliceMask */
      uint64_t subslice_mask;       /** $SubsliceMask */
      uint64_t gt_min_freq;         /** $GpuMinFrequency */
      uint64_t gt_max_freq;         /** $GpuMaxFrequency */
      uint64_t revision;            /** $SkuRevisionId */
   } sys_vars;

   /* OA metric sets, indexed by GUID, as know by Mesa at build time, to
    * cross-reference with the GUIDs of configs advertised by the kernel at
    * runtime
    */
   struct hash_table *oa_metrics_table;

   /* Location of the device's sysfs entry. */
   char sysfs_dev_dir[256];

   int (*ioctl)(int, unsigned long, void *);
};

static inline size_t
gen_perf_query_counter_get_size(const struct gen_perf_query_counter *counter)
{
   switch (counter->data_type) {
   case GEN_PERF_COUNTER_DATA_TYPE_BOOL32:
      return sizeof(uint32_t);
   case GEN_PERF_COUNTER_DATA_TYPE_UINT32:
      return sizeof(uint32_t);
   case GEN_PERF_COUNTER_DATA_TYPE_UINT64:
      return sizeof(uint64_t);
   case GEN_PERF_COUNTER_DATA_TYPE_FLOAT:
      return sizeof(float);
   case GEN_PERF_COUNTER_DATA_TYPE_DOUBLE:
      return sizeof(double);
   default:
      unreachable("invalid counter data type");
   }
}

static inline struct gen_perf_query_info *
gen_perf_query_append_query_info(struct gen_perf *perf, int max_counters)
{
   struct gen_perf_query_info *query;

   perf->queries = reralloc(perf, perf->queries,
                            struct gen_perf_query_info,
                            ++perf->n_queries);
   query = &perf->queries[perf->n_queries - 1];
   memset(query, 0, sizeof(*query));

   if (max_counters > 0) {
      query->max_counters = max_counters;
      query->counters =
         rzalloc_array(perf, struct gen_perf_query_counter, max_counters);
   }

   return query;
}

static inline void
gen_perf_query_info_add_stat_reg(struct gen_perf_query_info *query,
                                 uint32_t reg,
                                 uint32_t numerator,
                                 uint32_t denominator,
                                 const char *name,
                                 const char *description)
{
   struct gen_perf_query_counter *counter;

   assert(query->n_counters < query->max_counters);

   counter = &query->counters[query->n_counters];
   counter->name = name;
   counter->desc = description;
   counter->type = GEN_PERF_COUNTER_TYPE_RAW;
   counter->data_type = GEN_PERF_COUNTER_DATA_TYPE_UINT64;
   counter->offset = sizeof(uint64_t) * query->n_counters;
   counter->pipeline_stat.reg = reg;
   counter->pipeline_stat.numerator = numerator;
   counter->pipeline_stat.denominator = denominator;

   query->n_counters++;
}

static inline void
gen_perf_query_info_add_basic_stat_reg(struct gen_perf_query_info *query,
                                       uint32_t reg, const char *name)
{
   gen_perf_query_info_add_stat_reg(query, reg, 1, 1, name, name);
}

static inline struct gen_perf *
gen_perf_new(void *ctx, int (*ioctl_cb)(int, unsigned long, void *))
{
   struct gen_perf *perf = rzalloc(ctx, struct gen_perf);

   perf->ioctl = ioctl_cb;

   return perf;
}

bool gen_perf_load_oa_metrics(struct gen_perf *perf, int fd,
                              const struct gen_device_info *devinfo);
bool gen_perf_load_metric_id(struct gen_perf *perf, const char *guid,
                             uint64_t *metric_id);

void gen_perf_query_result_read_frequencies(struct gen_perf_query_result *result,
                                            const struct gen_device_info *devinfo,
                                            const uint32_t *start,
                                            const uint32_t *end);
void gen_perf_query_result_accumulate(struct gen_perf_query_result *result,
                                      const struct gen_perf_query_info *query,
                                      const uint32_t *start,
                                      const uint32_t *end);
void gen_perf_query_result_clear(struct gen_perf_query_result *result);


#endif /* GEN_PERF_H */
