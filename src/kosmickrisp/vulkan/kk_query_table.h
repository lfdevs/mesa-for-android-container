/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_QUERY_TABLE_H
#define KK_QUERY_TABLE_H 1

#include "kk_private.h"

#include "kk_bo.h"

#include "util/bitset.h"
#include "util/simple_mtx.h"

struct kk_device;

struct kk_query_table {
   simple_mtx_t mutex;

   uint32_t max_alloc;  /**< Maximum possible number of queries */
   uint32_t next_query; /**< Next unallocated query */
   uint32_t free_count; /**< Size of free_table */

   struct kk_bo *bo; /**< Memoery where queries are stored */

   /* Bitset of all queries currently in use.  This is the single source
    * of truth for what is and isn't free.  The free_table and next_query are
    * simply hints to make finding a free descrptor fast.  Every free
    * query will either be above next_query or in free_table but not
    * everything which satisfies those two criteria is actually free.
    */
   BITSET_WORD *in_use;

   /* Stack for free query elements */
   uint32_t *free_table;
};

VkResult kk_query_table_init(struct kk_device *dev,
                             struct kk_query_table *table,
                             uint32_t query_count);

void kk_query_table_finish(struct kk_device *dev, struct kk_query_table *table);

VkResult kk_query_table_add(struct kk_device *dev, struct kk_query_table *table,
                            uint64_t value, uint32_t *index_out);

VkResult kk_query_table_insert(struct kk_device *dev,
                               struct kk_query_table *table, uint32_t index,
                               uint64_t value);

void kk_query_table_remove(struct kk_device *dev, struct kk_query_table *table,
                           uint32_t index);

#endif /* KK_QUERY_TABLE_H */
