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

#if defined(MAJOR_IN_SYSMACROS)
#include <sys/sysmacros.h>
#elif defined(MAJOR_IN_MKDEV)
#include <sys/mkdev.h>
#endif

#include "util/hash_table.h"
#include "compiler/glsl/list.h"
#include "util/ralloc.h"

struct gen_device_info;

struct gen_perf_config;
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

/*
 * When currently allocate only one page for pipeline statistics queries. Here
 * we derived the maximum number of counters for that amount.
 */
#define STATS_BO_SIZE               4096
#define STATS_BO_END_OFFSET_BYTES   (STATS_BO_SIZE / 2)
#define MAX_STAT_COUNTERS           (STATS_BO_END_OFFSET_BYTES / 8)

#define I915_PERF_OA_SAMPLE_SIZE (8 +   /* drm_i915_perf_record_header */ \
                                  256)  /* OA counter report */

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

   /**
    * Timestamp of the query.
    */
   uint64_t begin_timestamp;

   /**
    * Whether the query was interrupted by another workload (aka preemption).
    */
   bool query_disjoint;
};

struct gen_perf_query_counter {
   const char *name;
   const char *desc;
   enum gen_perf_counter_type type;
   enum gen_perf_counter_data_type data_type;
   uint64_t raw_max;
   size_t offset;

   union {
      uint64_t (*oa_counter_read_uint64)(struct gen_perf_config *perf,
                                         const struct gen_perf_query_info *query,
                                         const uint64_t *accumulator);
      float (*oa_counter_read_float)(struct gen_perf_config *perf,
                                     const struct gen_perf_query_info *query,
                                     const uint64_t *accumulator);
      struct gen_pipeline_stat pipeline_stat;
   };
};

struct gen_perf_query_register_prog {
   uint32_t reg;
   uint32_t val;
};

/* Register programming for a given query */
struct gen_perf_registers {
   struct gen_perf_query_register_prog *flex_regs;
   uint32_t n_flex_regs;

   struct gen_perf_query_register_prog *mux_regs;
   uint32_t n_mux_regs;

   struct gen_perf_query_register_prog *b_counter_regs;
   uint32_t n_b_counter_regs;
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

   struct gen_perf_registers config;
};

struct gen_perf_config {
   bool i915_query_supported;

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

   struct {
      void *(*bo_alloc)(void *bufmgr, const char *name, uint64_t size);
      void (*bo_unreference)(void *bo);
      void *(*bo_map)(void *ctx, void *bo, unsigned flags);
      void (*bo_unmap)(void *bo);
      bool (*batch_references)(void *batch, void *bo);
      void (*bo_wait_rendering)(void *bo);
      int (*bo_busy)(void *bo);
      void (*emit_mi_flush)(void *ctx);
      void (*emit_mi_report_perf_count)(void *ctx,
                                        void *bo,
                                        uint32_t offset_in_bytes,
                                        uint32_t report_id);
      void (*batchbuffer_flush)(void *ctx,
                                const char *file, int line);
      void (*capture_frequency_stat_register)(void *ctx, void *bo,
                                              uint32_t bo_offset);
      void (*store_register_mem64)(void *ctx, void *bo, uint32_t reg, uint32_t offset);

   } vtbl;
};

struct gen_perf_query_object;
const struct gen_perf_query_info* gen_perf_query_info(const struct gen_perf_query_object *);

void gen_perf_init_metrics(struct gen_perf_config *perf_cfg,
                           const struct gen_device_info *devinfo,
                           int drm_fd);

/** Query i915 for a metric id using guid.
 */
bool gen_perf_load_metric_id(struct gen_perf_config *perf_cfg,
                             const char *guid,
                             uint64_t *metric_id);

/** Load a configuation's content from i915 using a guid.
 */
struct gen_perf_registers *gen_perf_load_configuration(struct gen_perf_config *perf_cfg,
                                                      int fd, const char *guid);

/** Store a configuration into i915 using guid and return a new metric id.
 *
 * If guid is NULL, then a generated one will be provided by hashing the
 * content of the configuration.
 */
uint64_t gen_perf_store_configuration(struct gen_perf_config *perf_cfg, int fd,
                                      const struct gen_perf_registers *config,
                                      const char *guid);

/** Read the slice/unslice frequency from 2 OA reports and store then into
 *  result.
 */
void gen_perf_query_result_read_frequencies(struct gen_perf_query_result *result,
                                            const struct gen_device_info *devinfo,
                                            const uint32_t *start,
                                            const uint32_t *end);
/** Accumulate the delta between 2 OA reports into result for a given query.
 */
void gen_perf_query_result_accumulate(struct gen_perf_query_result *result,
                                      const struct gen_perf_query_info *query,
                                      const uint32_t *start,
                                      const uint32_t *end);
void gen_perf_query_result_clear(struct gen_perf_query_result *result);

struct gen_perf_context;
struct gen_perf_context *gen_perf_new_context(void *parent);

void gen_perf_init_context(struct gen_perf_context *perf_ctx,
                           struct gen_perf_config *perf_cfg,
                           void * ctx,  /* driver context (eg, brw_context) */
                           void * bufmgr,  /* eg brw_bufmgr */
                           const struct gen_device_info *devinfo,
                           uint32_t hw_ctx,
                           int drm_fd);

struct gen_perf_config *gen_perf_config(struct gen_perf_context *ctx);

int gen_perf_active_queries(struct gen_perf_context *perf_ctx,
                            const struct gen_perf_query_info *query);

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

static inline struct gen_perf_config *
gen_perf_new(void *ctx)
{
   struct gen_perf_config *perf = rzalloc(ctx, struct gen_perf_config);
   return perf;
}

struct gen_perf_query_object *
gen_perf_new_query(struct gen_perf_context *, unsigned query_index);


bool gen_perf_begin_query(struct gen_perf_context *perf_ctx,
                          struct gen_perf_query_object *query);
void gen_perf_end_query(struct gen_perf_context *perf_ctx,
                        struct gen_perf_query_object *query);
void gen_perf_wait_query(struct gen_perf_context *perf_ctx,
                         struct gen_perf_query_object *query,
                         void *current_batch);
bool gen_perf_is_query_ready(struct gen_perf_context *perf_ctx,
                             struct gen_perf_query_object *query,
                             void *current_batch);
void gen_perf_delete_query(struct gen_perf_context *perf_ctx,
                           struct gen_perf_query_object *query);
void gen_perf_get_query_data(struct gen_perf_context *perf_ctx,
                             struct gen_perf_query_object *query,
                             int data_size,
                             unsigned *data,
                             unsigned *bytes_written);

void gen_perf_dump_query_count(struct gen_perf_context *perf_ctx);
void gen_perf_dump_query(struct gen_perf_context *perf_ctx,
                         struct gen_perf_query_object *obj,
                         void *current_batch);

#endif /* GEN_PERF_H */
