/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_QUERY_POOL_H
#define TU_QUERY_POOL_H

#include "tu_common.h"
#include "perfcntrs/freedreno_perfcntr.h"

#include "vk_query_pool.h"

#define PERF_CNTRS_REG 4

enum tu_perf_query_type {
   TU_PERF_QUERY_TYPE_NONE,
   TU_PERF_QUERY_TYPE_RAW,
   TU_PERF_QUERY_TYPE_DERIVED,
};

struct tu_perf_query_raw_data
{
   const struct fd_perfcntr_counter *counter;
   const struct fd_perfcntr_countable *countable;
   uint32_t pass;     /* pass index that countables can be requested */
   uint32_t app_idx;  /* index provided by apps */
};

struct tu_perf_query_raw {
   const struct fd_perfcntr_group *perf_group;
   uint32_t perf_group_count;
   uint32_t counter_index_count;
   struct tu_perf_query_raw_data data[0];
};

struct tu_perf_query_derived {
   const struct fd_derived_counter **derived_counters;
   uint32_t derived_counters_count;

   uint32_t counter_index_count;
   struct fd_derived_counter_collection collection[0];
};

struct tu_query_pool
{
   struct vk_query_pool vk;

   uint64_t size;
   uint32_t query_stride;
   struct tu_bo *bo;

   /* For performance query */
   enum tu_perf_query_type perf_query_type;
   union {
      struct tu_perf_query_raw raw;
      struct tu_perf_query_derived derived;
   } perf_query;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_query_pool, vk.base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

#endif /* TU_QUERY_POOL_H */
