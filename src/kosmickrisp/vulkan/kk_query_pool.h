/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_QUERY_POOL_H
#define KK_QUERY_POOL_H 1

#include "kk_private.h"

#include "util/u_dynarray.h"
#include "vulkan/runtime/vk_query_pool.h"

struct kk_ts_stage_entry {
   mtl_render_encoder *pass;
   enum mtl_render_stages stage;
   uint16_t index;
};

struct kk_ts_state {
   /* Metal 4 counter heap backing VK_QUERY_TYPE_TIMESTAMP pools. Timestamps are
    * sampled into this heap and later resolved into `bo` (see kk_query_pool.c).
    * NULL for non-timestamp pools. */
   mtl_counter_heap *heap;
   struct util_dynarray stage_map;
};

struct kk_oq_state {
   unsigned queries;
};

struct kk_query_pool {
   struct vk_query_pool vk;

   struct kk_bo *bo;

   uint32_t index_start;
   uint32_t query_start;
   uint32_t query_stride;

   union {
      struct kk_ts_state ts;
      struct kk_oq_state oq;
   };
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_query_pool, vk.base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

uint16_t *kk_pool_index_ptr(const struct kk_query_pool *pool);

#endif /* KK_QUERY_POOL_H */
