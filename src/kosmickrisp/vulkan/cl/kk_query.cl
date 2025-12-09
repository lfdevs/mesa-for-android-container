/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright © 2024 Alyssa Rosenzweig
 * Copyright © 2024 Valve Corporation
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl_vk.h"

#include "kk_query.h"

void
libkk_write_u64(global struct libkk_imm_write *write_array)
{
   *write_array[cl_group_id.x].address = write_array[cl_group_id.x].value;
}

void
libkk_copy_queries(global uint64_t *availability, global uint64_t *results,
                   global uint16_t *oq_index, uint64_t dst_addr,
                   uint64_t dst_stride, uint32_t first_query,
                   VkQueryResultFlagBits flags, uint16_t reports_per_query)
{
   uint index = cl_group_id.x;
   uint64_t dst = dst_addr + (((uint64_t)index) * dst_stride);
   uint32_t query = first_query + index;

   bool available;
   if (availability)
      available = availability[query];
   else
      available = (results[query] != LIBKK_QUERY_UNAVAILABLE);

   if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
      /* For occlusion queries, results[] points to the device global heap. We
       * need to remap indices according to the query pool's allocation.
       */
      uint result_index = oq_index ? oq_index[query] : query;
      uint idx = result_index * reports_per_query;

      for (unsigned i = 0; i < reports_per_query; ++i) {
         vk_write_query(dst, i, flags, results[idx + i]);
      }
   }

   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
      vk_write_query(dst, reports_per_query, flags, available);
   }
}
