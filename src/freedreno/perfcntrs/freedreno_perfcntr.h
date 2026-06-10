/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_PERFCNTR_H_
#define FREEDRENO_PERFCNTR_H_

#include "util/macros.h"

#include "freedreno_dev_info.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mapping very closely to the AMD_performance_monitor extension, adreno has
 * groups of performance counters where each group has N counters, which can
 * select from M different countables (things that can be counted), where
 * generally M > N.
 */

/* Describes a single counter: */
struct fd_perfcntr_counter {
   /* offset of the SELect register to choose what to count: */
   unsigned select_reg;
   /* additional SEL regs to enable slice counters (gen8+) */
   unsigned slice_select_regs[2];
   /* offset of the lo/hi 32b to read current counter value: */
   unsigned counter_reg_lo;
   unsigned counter_reg_hi;
   /* Optional, most counters don't have enable/clear registers: */
   unsigned enable;
   unsigned clear;
};

enum fd_perfcntr_type {
   FD_PERFCNTR_TYPE_UINT64,
   FD_PERFCNTR_TYPE_UINT,
   FD_PERFCNTR_TYPE_FLOAT,
   FD_PERFCNTR_TYPE_DOUBLE,
   FD_PERFCNTR_TYPE_PERCENTAGE,
   FD_PERFCNTR_TYPE_BYTES,
   FD_PERFCNTR_TYPE_MICROSECONDS,
   FD_PERFCNTR_TYPE_HZ,
   FD_PERFCNTR_TYPE_DBM,
   FD_PERFCNTR_TYPE_TEMPERATURE,
   FD_PERFCNTR_TYPE_VOLTS,
   FD_PERFCNTR_TYPE_AMPS,
   FD_PERFCNTR_TYPE_WATTS,
};

/* Whether an average value per frame or a cumulative value should be
 * displayed.
 */
enum fd_perfcntr_result_type {
   FD_PERFCNTR_RESULT_TYPE_AVERAGE,
   FD_PERFCNTR_RESULT_TYPE_CUMULATIVE,
};

/* Describes a single countable: */
struct fd_perfcntr_countable {
   const char *name;
   /* selector register enum value to select this countable: */
   unsigned selector;
};

/* Describes an entire counter group: */
struct fd_perfcntr_group {
   const char *name;
   int pipe;
   unsigned num_counters;
   const struct fd_perfcntr_counter *counters;
   unsigned num_countables;
   const struct fd_perfcntr_countable *countables;
};

const struct fd_perfcntr_group *fd_perfcntrs(const struct fd_dev_id *id, unsigned *count);

#define GROUP(_name, _pipe, _counters, _countables) {                          \
      .name = _name,                                                           \
      .pipe = _pipe,                                                           \
      .num_counters = ARRAY_SIZE(_counters),                                   \
      .counters = _counters, .num_countables = ARRAY_SIZE(_countables),        \
      .countables = _countables,                                               \
   }

static inline const struct fd_perfcntr_group *
fd_perfcntrs_group(const struct fd_dev_id *id, const char *name)
{
   const struct fd_perfcntr_group *groups;
   unsigned count;

   groups = fd_perfcntrs(id, &count);
   if (!groups)
      return NULL;

   for (unsigned i = 0; i < count; i++)
      if (!strcmp(groups[i].name, name))
         return &groups[i];

   return NULL;
}

static inline const struct fd_perfcntr_countable *
fd_perfcntrs_countable(const struct fd_perfcntr_group *group, const char *name)
{
   for (unsigned i = 0; i < group->num_countables; i++)
      if (!strcmp(group->countables[i].name, name))
         return &group->countables[i];

   return NULL;
}

struct fd_perfcntr_state;

struct fd_perfcntr_state *
fd_perfcntr_state_alloc(const struct fd_dev_id *id, int fd);
void fd_perfcntr_state_free(struct fd_perfcntr_state *perfcntrs);

bool fd_perfcntr_has_reservation(struct fd_perfcntr_state *perfcntrs);

const struct fd_perfcntr_counter *
fd_perfcntr_reserve(struct fd_perfcntr_state *perfcntrs,
                    const struct fd_perfcntr_group *group,
                    const struct fd_perfcntr_countable *countable);
void fd_perfcntr_release(struct fd_perfcntr_state *perfcntrs,
                         const struct fd_perfcntr_counter *counter);

#define FD_DERIVED_COUNTER_MAX_PERFCNTRS 8

struct fd_derivation_context {
      struct {
         uint32_t number_of_usptp;
         uint32_t number_of_alus_per_usptp;
      } a7xx;
};

struct fd_derived_counter {
   const char *name;
   const char *description;
   const char *category;

   enum fd_perfcntr_type type;
   unsigned num_perfcntrs;
   uint8_t perfcntrs[FD_DERIVED_COUNTER_MAX_PERFCNTRS];

   uint64_t (*derive)(struct fd_derivation_context *context, uint64_t *values);
};

struct fd_derived_counter_perfcntr {
   const char *countable;
   const char *group;
};

const struct fd_derived_counter **fd_derived_counters(const struct fd_dev_id *id, unsigned *count);

#define FD_DERIVED_COUNTER_COLLECTION_MAX_DERIVED_COUNTERS 64
#define FD_DERIVED_COUNTER_COLLECTION_MAX_ENABLED_PERFCNTRS 128

struct fd_derived_counter_collection {
   unsigned num_counters;
   const struct fd_derived_counter *counters[FD_DERIVED_COUNTER_COLLECTION_MAX_DERIVED_COUNTERS];

   bool cp_always_count_enabled;
   unsigned num_enabled_perfcntrs;
   struct {
      const struct fd_perfcntr_counter *counter;
      unsigned countable;
   } enabled_perfcntrs[FD_DERIVED_COUNTER_COLLECTION_MAX_ENABLED_PERFCNTRS];
   uint8_t enabled_perfcntrs_map[FD_DERIVED_COUNTER_COLLECTION_MAX_ENABLED_PERFCNTRS];

   struct fd_derivation_context derivation_context;
};

void fd_reserve_derived_counter_collection(struct fd_perfcntr_state *perfcntrs, struct fd_derived_counter_collection *collection);
void fd_release_derived_counter_collection(struct fd_perfcntr_state *perfcntrs, struct fd_derived_counter_collection *collection);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* FREEDRENO_PERFCNTR_H_ */
