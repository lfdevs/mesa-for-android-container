/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_QUERY_H
#define PANVK_CMD_QUERY_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "util/bitscan.h"

#include "genxml/gen_macros.h"

struct panvk_occlusion_query_state {
#if PAN_ARCH >= 10
   uint64_t syncobj;
#endif
   uint64_t ptr;
   enum mali_occlusion_mode mode;
};

#if PAN_ARCH >= 10
struct panvk_prims_generated_query_state {
   uint64_t syncobj;
   uint64_t ptr;
};

/* A pipeline statistics query has two u64 per enabled statistic, in bit
 * order: the first half is only written by the command stream, the second
 * half only by shader atomics and the occlusion counter, so neither needs to
 * wait on the other. Results are the sum of both halves.
 */
struct panvk_pstats_query_state {
   uint64_t report;
   uint32_t mask;
   /* Stand-in occlusion query syncobj while the FS invocation counter
    * borrows the hardware occlusion counter. */
   uint64_t oq_sync;
};

#define PANVK_PSTATS_MAX_PENDING 8

struct panvk_pstats_pending {
   uint64_t avail;
   uint32_t view_count;
};

static inline uint64_t
panvk_pstats_counter_addr(const struct panvk_pstats_query_state *q,
                          uint32_t bit, bool kernel_half)
{
   if (!q->report || !(q->mask & bit))
      return 0;

   uint32_t slot = util_bitcount(q->mask & (bit - 1)) +
                   (kernel_half ? util_bitcount(q->mask) : 0);
   return q->report + slot * sizeof(uint64_t);
}
#endif

#endif
