/*
 * Copyright © 2019 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <stddef.h>

#include "util/hash_table.h"
#include "util/libdrm.h"
#include "util/ralloc.h"

#include "drm-uapi/msm_drm.h"
#include "util/bitset.h"
#include "util/simple_mtx.h"
#include "freedreno_common.h"

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
   case 8:
      *count = a8xx_num_perfcntr_groups;
      return a8xx_perfcntr_groups;
   default:
      *count = 0;
      return NULL;
   }
}

struct fd_perfcntr_counter_state {
   int group;
   int counter;
   int countable;
   unsigned nr_users;
};

#define MAX_COUNTERS_PER_GROUP 32
typedef BITSET_DECLARE(assigned_counters_t, MAX_COUNTERS_PER_GROUP);

/**
 * Helper to manage assigning counters, tracking if there are multiple users
 * for the same countable (to avoid assigning duplicate counters for the
 * same countable, etc)
 */
struct fd_perfcntr_state {
   simple_mtx_t lock;
   int fd;
   const struct fd_dev_id *id;

   unsigned nr_groups;
   const struct fd_perfcntr_group *groups;

   struct drm_msm_perfcntr_group *group_configs;
   struct drm_msm_perfcntr_config config;

   /* bitmask of assigned counters per group: */
   assigned_counters_t *assigned_counters;

   /* maps counter to fd_perfcntr_counter_state: */
   struct hash_table *counter_state;
};

static int
update_reserved_counters(struct fd_perfcntr_state *perfcntrs)
{
#ifdef HAVE_LIBDRM
   /* If no kernel support, just carry on and assume we can use all counters: */
   if (perfcntrs->fd < 0)
      return 0;

   return drmIoctl(perfcntrs->fd, DRM_IOCTL_MSM_PERFCNTR_CONFIG, &perfcntrs->config);
#else
   return 0;
#endif
}

static int
update_group_counters(struct fd_perfcntr_state *perfcntrs, int group_idx)
{
   int ret = 0;

   /* Update reserved config with kernel if it changes.  We might not
    * be assiging/releasing the last counter (and we cannot feasibly
    * re-map existing assigned counters to compact away gaps in the
    * used counters, as cmdstream might already
    * be built encoding the other assigned counters), but if we do
    * let the kernel know:
    */
   unsigned nr = BITSET_LAST_BIT(perfcntrs->assigned_counters[group_idx]);
   if (nr != perfcntrs->group_configs[group_idx].nr_countables) {
      perfcntrs->group_configs[group_idx].nr_countables = nr;
      ret = update_reserved_counters(perfcntrs);
   }

   return ret;
}

struct fd_perfcntr_state *
fd_perfcntr_state_alloc(const struct fd_dev_id *id, int fd)
{
   const struct fd_perfcntr_group *groups;
   unsigned nr_groups;

   groups = fd_perfcntrs(id, &nr_groups);
   if (!groups)
      return NULL;

   struct fd_perfcntr_state *perfcntrs = rzalloc(NULL, struct fd_perfcntr_state);

   simple_mtx_init(&perfcntrs->lock, mtx_plain);
   perfcntrs->fd = fd;
   perfcntrs->id = id;
   perfcntrs->nr_groups = nr_groups;
   perfcntrs->groups = groups;
   perfcntrs->group_configs =
      rzalloc_array(perfcntrs, struct drm_msm_perfcntr_group, nr_groups);

   for (unsigned i = 0; i < nr_groups; i++) {
      assert(strlen(groups[i].name) < sizeof(perfcntrs->group_configs[i].group_name));
      strcpy(perfcntrs->group_configs[i].group_name, groups[i].name);
   }

   perfcntrs->config = (struct drm_msm_perfcntr_config) {
      .nr_groups = nr_groups,
      .groups = VOID2U64(perfcntrs->group_configs),
      .group_stride = sizeof(struct drm_msm_perfcntr_group),
   };

   perfcntrs->assigned_counters = rzalloc_array(perfcntrs, assigned_counters_t, nr_groups);
   perfcntrs->counter_state = _mesa_pointer_hash_table_create(perfcntrs);

   /* Probe for kernel PERFCNTR_CONFIG support with empty config: */
   if (update_reserved_counters(perfcntrs))
      perfcntrs->fd = -1;

   return perfcntrs;
}

void
fd_perfcntr_state_free(struct fd_perfcntr_state *perfcntrs)
{
   if (!perfcntrs)
      return;

   perfcntrs->config.nr_groups = 0;
   update_reserved_counters(perfcntrs);
   ralloc_free(perfcntrs);
}

/**
 * Does KMD support perfcntr reservation (ie. PERFCNTR_CONFIG)
 */
bool
fd_perfcntr_has_reservation(struct fd_perfcntr_state *perfcntrs)
{
   return perfcntrs->fd >= 0;
}

static int
find_group_idx(struct fd_perfcntr_state *perfcntrs,
               const struct fd_perfcntr_group *group)
{
   for (unsigned i = 0; i < perfcntrs->nr_groups; i++)
      if (&perfcntrs->groups[i] == group)
         return i;
   UNREACHABLE("invalid group");
}

static int
find_countable_idx(const struct fd_perfcntr_group *group,
                   const struct fd_perfcntr_countable *countable)
{
   for (unsigned i = 0; i < group->num_countables; i++)
      if (&group->countables[i] == countable)
         return i;
   UNREACHABLE("invalid countable");
}

const struct fd_perfcntr_counter *
fd_perfcntr_reserve(struct fd_perfcntr_state *perfcntrs,
                    const struct fd_perfcntr_group *group,
                    const struct fd_perfcntr_countable *countable)
{
   struct fd_perfcntr_counter_state *state = NULL;
   int c, g = find_group_idx(perfcntrs, group);

   simple_mtx_lock(&perfcntrs->lock);

   /* Check if requested countable is already configured: */
   BITSET_FOREACH_SET (c, perfcntrs->assigned_counters[g], MAX_COUNTERS_PER_GROUP) {
      struct hash_entry *e =
         _mesa_hash_table_search(perfcntrs->counter_state, &group->counters[c]);

      assert(e);
      struct fd_perfcntr_counter_state *s = e->data;

      if (&group->countables[s->countable] == countable) {
         state = s;
         break;
      }
   }

   /* If we didn't find a counter assigned to this countable, assign a new one: */
   if (!state) {
      assigned_counters_t *assigned_counters = &perfcntrs->assigned_counters[g];

      /* Pick lowest #ed unassigned counter: */
      assigned_counters_t free_counters;
      memcpy(free_counters, *assigned_counters, sizeof(free_counters));
      BITSET_NOT(free_counters);

      c = BITSET_FFS(free_counters) - 1;
      assert(c >= 0);

      if (c < group->num_counters) {
         state = rzalloc(perfcntrs, struct fd_perfcntr_counter_state);
         state->group = g;
         state->counter = c;
         state->countable = find_countable_idx(group, countable);

         assert(!BITSET_TEST(*assigned_counters, state->counter));

         BITSET_SET(*assigned_counters, state->counter);

         if (update_group_counters(perfcntrs, state->group)) {
            BITSET_CLEAR(*assigned_counters, state->counter);
            ralloc_free(state);
            state = NULL;
         } else {
            _mesa_hash_table_insert(perfcntrs->counter_state,
                                    &group->counters[state->counter],
                                    state);
         }
      }
   }

   if (state)
      state->nr_users++;

   simple_mtx_unlock(&perfcntrs->lock);

   if (!state)
      return NULL;

   return &group->counters[state->counter];
}

void
fd_perfcntr_release(struct fd_perfcntr_state *perfcntrs,
                    const struct fd_perfcntr_counter *counter)
{
   if (!counter)
      return;

   simple_mtx_lock(&perfcntrs->lock);
   struct hash_entry *e = _mesa_hash_table_search(perfcntrs->counter_state, counter);
   if (e) {
      struct fd_perfcntr_counter_state *state = e->data;

      assert(state->nr_users > 0);

      if (--state->nr_users == 0) {
         /* dropping last user of the counter: */
         _mesa_hash_table_remove(perfcntrs->counter_state, e);

         assigned_counters_t *assigned_counters =
            &perfcntrs->assigned_counters[state->group];

         assert(BITSET_TEST(*assigned_counters, state->counter));

         BITSET_CLEAR(*assigned_counters, state->counter);
         update_group_counters(perfcntrs, state->group);

         ralloc_free(state);
      }
   }
   simple_mtx_unlock(&perfcntrs->lock);
}

extern const struct fd_derived_counter_perfcntr a7xx_derived_counter_perfcntrs[];
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

void
fd_reserve_derived_counter_collection(struct fd_perfcntr_state *perfcntrs, struct fd_derived_counter_collection *collection)
{
   const struct fd_derived_counter_perfcntr *derived_counter_perfcntrs = NULL;
   const struct fd_dev_id *id = perfcntrs->id;

   switch (fd_dev_gen(id)) {
   case 7:
      derived_counter_perfcntrs = a7xx_derived_counter_perfcntrs;
      break;
   default:
      return;
   }

   /* The provided collection should already specify the derived counters that will be measured.
    * This function will set up enabled_perfcntrs_map and enabled_perfcntrs array so that each
    * used DERIVED_COUNTER_PERFCNTR_* enum value will map to the corresponding index in the
    * array where the relevant fd_perfcntr_counter and fd_perfcntr_countable are stored.
    */

   collection->num_enabled_perfcntrs = 0;
   memset(collection->enabled_perfcntrs_map, 0xff, ARRAY_SIZE(collection->enabled_perfcntrs_map));

   for (unsigned i = 0; i < collection->num_counters; ++i) {
      const struct fd_derived_counter *counter = collection->counters[i];

      for (unsigned j = 0; j < counter->num_perfcntrs; ++j) {
         uint8_t perfcntr = counter->perfcntrs[j];
         collection->enabled_perfcntrs_map[perfcntr] = 0x00;
      }
   }

   /* Note if CP_ALWAYS_COUNT is enabled. This is the zero-index perfcntr. */
   collection->cp_always_count_enabled = !collection->enabled_perfcntrs_map[0];

   for (unsigned i = 0; i < ARRAY_SIZE(collection->enabled_perfcntrs_map); ++i) {
      if (collection->enabled_perfcntrs_map[i] == 0xff)
         continue;

      uint8_t enabled_perfcntr_index = collection->num_enabled_perfcntrs++;
      collection->enabled_perfcntrs_map[i] = enabled_perfcntr_index;

      const struct fd_perfcntr_group *group =
         fd_perfcntrs_group(perfcntrs->id, derived_counter_perfcntrs[i].group);
      const struct fd_perfcntr_countable *countable =
         fd_perfcntrs_countable(group, derived_counter_perfcntrs[i].countable);
      const struct fd_perfcntr_counter *counter =
         fd_perfcntr_reserve(perfcntrs, group, countable);

      collection->enabled_perfcntrs[enabled_perfcntr_index].counter = counter;
      collection->enabled_perfcntrs[enabled_perfcntr_index].countable = countable->selector;
   }

   const struct fd_dev_info *info = fd_dev_info_raw(id);
   switch (fd_dev_gen(id)) {
   case 7:
      collection->derivation_context.a7xx.number_of_usptp = info->num_sp_cores * 2;
      collection->derivation_context.a7xx.number_of_alus_per_usptp = 128;
      break;
   default:
      break;
   }
}

void
fd_release_derived_counter_collection(struct fd_perfcntr_state *perfcntrs, struct fd_derived_counter_collection *collection)
{
   for (unsigned i = 0; i < collection->num_enabled_perfcntrs; i++)
      fd_perfcntr_release(perfcntrs, collection->enabled_perfcntrs[i].counter);
}
