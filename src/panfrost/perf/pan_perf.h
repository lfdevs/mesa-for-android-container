/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_PERF_H
#define PAN_PERF_H

#include <stdint.h>

#include <lib/kmod/pan_kmod.h>

#include "mali_perf.h"

#include "util/timespec.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct pan_perf {
   struct pan_kmod_dev *dev;
   const struct mali_perf_gpu_info *info;
   struct pan_kmod_perf_session *session;
   struct mali_perf_constants constants;
   struct mali_perf_dump_info dump_info;
   const struct pan_perf_config *cfg;
   uint64_t sampling_period_ns;
};

int64_t pan_perf_counter_read(const struct pan_perf *perf,
                              const struct mali_perf_counter *counter,
                              uint8_t blk_idx);

int64_t pan_perf_counter_read_sum(const struct pan_perf *perf,
                                  const struct mali_perf_counter *counter);

struct pan_perf *pan_perf_create(int fd);

void pan_perf_destroy(struct pan_perf *perf);

int pan_perf_enable(struct pan_perf *perf, uint64_t sampling_period_ns);

int pan_perf_disable(struct pan_perf *perf);

int pan_perf_dump(struct pan_perf *perf);

static inline clockid_t
pan_perf_gpu_clock_id(const struct pan_perf *perf)
{
   return CLOCK_MONOTONIC_RAW;
}

static inline uint64_t
pan_perf_get_gpu_timestamp(const struct pan_perf *perf)
{
   return pan_kmod_timestamp_cycles_to_ns(
      perf->session->dev, pan_kmod_query_timestamp(perf->session->dev));
}

static inline uint64_t
pan_perf_get_dump_timestamp(const struct pan_perf *perf)
{
   return perf->dump_info.time_span.end_ns;
}

static inline uint64_t
pan_perf_get_min_sampling_period(const struct pan_perf *perf)
{
   return perf->session->caps.min_sampling_period_ns;
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // PAN_PERF_H
