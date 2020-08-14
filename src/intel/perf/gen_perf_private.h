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

#ifndef GEN_PERF_PRIVATE_H
#define GEN_PERF_PRIVATE_H

#include "gen_perf.h"

static inline uint64_t to_user_pointer(void *ptr)
{
   return (uintptr_t) ptr;
}

static inline uint64_t to_const_user_pointer(const void *ptr)
{
   return (uintptr_t) ptr;
}

static inline void
gen_perf_query_add_stat_reg(struct gen_perf_query_info *query, uint32_t reg,
                            uint32_t numerator, uint32_t denominator,
                            const char *name, const char *description)
{
   struct gen_perf_query_counter *counter;

   assert(query->n_counters < query->max_counters);

   counter = &query->counters[query->n_counters];
   counter->name = counter->symbol_name = name;
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
gen_perf_query_add_basic_stat_reg(struct gen_perf_query_info *query,
                                  uint32_t reg, const char *name)
{
   gen_perf_query_add_stat_reg(query, reg, 1, 1, name, name);
}

static inline struct gen_perf_query_info *
gen_perf_append_query_info(struct gen_perf_config *perf, int max_counters)
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

void gen_perf_register_mdapi_statistic_query(struct gen_perf_config *perf_cfg,
                                             const struct gen_device_info *devinfo);
void gen_perf_register_mdapi_oa_query(struct gen_perf_config *perf,
                                      const struct gen_device_info *devinfo);


#endif /* GEN_PERF_PRIVATE_H */
