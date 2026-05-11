/*
 * Copyright © 2019 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <stddef.h>

#include "freedreno_perfcntr.h"

extern const struct fd_perfcntr_group a2xx_perfcntr_groups[];
extern const unsigned a2xx_num_perfcntr_groups;

extern const struct fd_perfcntr_group a5xx_perfcntr_groups[];
extern const unsigned a5xx_num_perfcntr_groups;

extern const struct fd_perfcntr_group a6xx_perfcntr_groups[];
extern const unsigned a6xx_num_perfcntr_groups;

extern const struct fd_perfcntr_group a7xx_perfcntr_groups[];
extern const unsigned a7xx_num_perfcntr_groups;

extern const struct fd_perfcntr_group a8xx_perfcntr_groups[];
extern const unsigned a8xx_num_perfcntr_groups;

const struct fd_perfcntr_group *
fd_perfcntrs(const struct fd_dev_id *id, unsigned *count)
{
   switch (fd_dev_gen(id)) {
   case 2:
      *count = a2xx_num_perfcntr_groups;
      return a2xx_perfcntr_groups;
   case 5:
      *count = a5xx_num_perfcntr_groups;
      return a5xx_perfcntr_groups;
   case 6:
      *count = a6xx_num_perfcntr_groups;
      return a6xx_perfcntr_groups;
   case 7:
      *count = a7xx_num_perfcntr_groups;
      return a7xx_perfcntr_groups;
   default:
      *count = 0;
      return NULL;
   }
}

extern const struct fd_derived_counter *a7xx_derived_counters[];
extern const unsigned a7xx_num_derived_counters;

const struct fd_derived_counter **
fd_derived_counters(const struct fd_dev_id *id, unsigned *count)
{
   switch (fd_dev_gen(id)) {
   case 7:
      *count = a7xx_num_derived_counters;
      return a7xx_derived_counters;
   default:
      *count = 0;
      return NULL;
   }
}

extern void a7xx_generate_derived_counter_collection(const struct fd_dev_id *id, struct fd_derived_counter_collection *collection);

void
fd_generate_derived_counter_collection(const struct fd_dev_id *id, struct fd_derived_counter_collection *collection)
{
   switch (fd_dev_gen(id)) {
   case 7:
      a7xx_generate_derived_counter_collection(id, collection);
      break;
   default:
      break;
   }
}
